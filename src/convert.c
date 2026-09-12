/* Cell-level predicates and conversions (design SS11-SS13).
 *
 * Everything here works on a {pointer, length} cell borrowed from the
 * parser's buffer: nothing is copied, and the cell is never modified. */

#include "zucsv.h"

#include <R_ext/Utils.h> /* R_strtod */
#include <string.h>

int zucsv_is_na(const zucsv_na *na, const unsigned char *str, size_t len) {
  for (R_xlen_t i = 0; i < na->n; i++) {
    if (na->len[i] == len && (len == 0 || memcmp(na->str[i], str, len) == 0))
      return 1;
  }
  return 0;
}

R_xlen_t zucsv_find_nul(const unsigned char *str, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (str[i] == '\0')
      return (R_xlen_t)i;
  }
  return -1;
}

/* Well-formed UTF-8 per the Unicode standard's definition: shortest form
   only, no surrogates, nothing above U+10FFFF. R itself rejects the
   sequences excluded here, so accepting them would only defer the failure
   to a later nchar() or print() with a worse message (design SS13). */
int zucsv_valid_utf8(const unsigned char *str, size_t len) {
  size_t i = 0;
  while (i < len) {
    unsigned char c = str[i];

    if (c < 0x80) {
      i++;
      continue;
    }

    size_t need;
    unsigned char lo, hi; /* permitted range of the first continuation byte */

    if (c >= 0xC2 && c <= 0xDF) {
      need = 1;
      lo = 0x80;
      hi = 0xBF;
    } else if (c == 0xE0) {
      need = 2;
      lo = 0xA0; /* excludes over-long 3-byte forms */
      hi = 0xBF;
    } else if (c >= 0xE1 && c <= 0xEC) {
      need = 2;
      lo = 0x80;
      hi = 0xBF;
    } else if (c == 0xED) {
      need = 2;
      lo = 0x80;
      hi = 0x9F; /* excludes UTF-16 surrogates D800-DFFF */
    } else if (c >= 0xEE && c <= 0xEF) {
      need = 2;
      lo = 0x80;
      hi = 0xBF;
    } else if (c == 0xF0) {
      need = 3;
      lo = 0x90; /* excludes over-long 4-byte forms */
      hi = 0xBF;
    } else if (c >= 0xF1 && c <= 0xF3) {
      need = 3;
      lo = 0x80;
      hi = 0xBF;
    } else if (c == 0xF4) {
      need = 3;
      lo = 0x80;
      hi = 0x8F; /* caps at U+10FFFF */
    } else {
      return 0; /* C0, C1, F5-FF, or a stray continuation byte */
    }

    /* i < len, so len - i >= 1; the sequence needs `need` more bytes. */
    if (need >= len - i)
      return 0; /* truncated sequence */

    if (str[i + 1] < lo || str[i + 1] > hi)
      return 0;
    for (size_t k = 2; k <= need; k++) {
      if (str[i + k] < 0x80 || str[i + k] > 0xBF)
        return 0;
    }
    i += need + 1;
  }
  return 1;
}

/* ------------------------------------------------------------------ *
 * Type grammars (design SS11)
 *
 * Each of these decides syntax only. Conversion happens separately, and
 * only once a column's type is fixed, so a cell's text is never reinterpreted
 * on the strength of a guess.
 * ------------------------------------------------------------------ */

int zucsv_is_logical(const unsigned char *str, size_t len) {
  /* Only these four spellings. T, F, True, 0 and 1 are not logical syntax
     in v0.1: they are far more often data than they are booleans. */
  if (len == 4)
    return memcmp(str, "TRUE", 4) == 0 || memcmp(str, "true", 4) == 0;
  if (len == 5)
    return memcmp(str, "FALSE", 5) == 0 || memcmp(str, "false", 5) == 0;
  return 0;
}

int zucsv_as_logical(const unsigned char *str, size_t len) {
  /* Only ever called on a cell zucsv_is_logical() accepted, so the first
     byte settles it. */
  (void)len;
  return str[0] == 'T' || str[0] == 't';
}

int zucsv_is_integer(const unsigned char *str, size_t len, int *out) {
  if (len == 0)
    return 0;

  size_t i = 0;
  int negative = 0;
  if (str[0] == '+' || str[0] == '-') {
    negative = (str[0] == '-');
    i = 1;
    if (len == 1)
      return 0; /* a bare sign is not a number */
  }

  /* Accumulate in 64 bits so the range check is exact and needs no libc
     call. Leading zeros are allowed, as in utils::type.convert(). */
  long long value = 0;
  for (; i < len; i++) {
    if (str[i] < '0' || str[i] > '9')
      return 0;
    value = value * 10 + (str[i] - '0');
    if (value > 2147483647LL)
      return 0; /* out of R's integer range; the column widens to double */
  }
  if (negative)
    value = -value;

  /* R uses INT_MIN as NA_integer_, so it is not an available value. */
  if (value < -2147483647LL)
    return 0;

  if (out)
    *out = (int)value;
  return 1;
}

int zucsv_is_double(const unsigned char *str, size_t len) {
  if (len == 0)
    return 0;

  size_t i = 0;
  if (str[0] == '+' || str[0] == '-')
    i = 1;

  /* Inf and NaN, case-sensitively: write.csv() emits exactly these, and
     accepting more spellings would start swallowing ordinary words. NaN
     takes no sign, matching R's own output. */
  if (len - i == 3) {
    if (memcmp(str + i, "Inf", 3) == 0)
      return 1;
    if (i == 0 && memcmp(str, "NaN", 3) == 0)
      return 1;
  }

  size_t digits_before = 0, digits_after = 0;
  while (i < len && str[i] >= '0' && str[i] <= '9') {
    digits_before++;
    i++;
  }
  if (i < len && str[i] == '.') {
    i++;
    while (i < len && str[i] >= '0' && str[i] <= '9') {
      digits_after++;
      i++;
    }
  }
  if (digits_before == 0 && digits_after == 0)
    return 0; /* ".", "+", "e5" and friends */

  if (i < len && (str[i] == 'e' || str[i] == 'E')) {
    i++;
    if (i < len && (str[i] == '+' || str[i] == '-'))
      i++;
    size_t exp_digits = 0;
    while (i < len && str[i] >= '0' && str[i] <= '9') {
      exp_digits++;
      i++;
    }
    if (exp_digits == 0)
      return 0; /* "1e", "1e+" */
  }

  return i == len; /* nothing left over: no trailing text, no whitespace */
}

double zucsv_as_double(const unsigned char *str, size_t len) {
  /* R_strtod is what as.numeric() uses, so results match base R exactly and
     are independent of LC_NUMERIC. It needs a NUL-terminated string, and the
     grammar check above has already bounded what can arrive here. */
  char stack_buf[64];
  char *buf = stack_buf;
  if (len + 1 > sizeof(stack_buf))
    buf = (char *)R_alloc(len + 1, 1);

  memcpy(buf, str, len);
  buf[len] = '\0';

  char *end;
  double value = R_strtod(buf, &end);
  return value;
}

/* ------------------------------------------------------------------ *
 * Per-column inference (design SS11)
 * ------------------------------------------------------------------ */

void zucsv_infer_init(zucsv_infer *st) {
  st->can_logical = 1;
  st->can_integer = 1;
  st->can_double = 1;
  st->all_missing = 1;
}

/* Called once per non-missing cell. Each flag only ever goes from 1 to 0,
   so the order rows arrive in cannot change the answer. */
void zucsv_infer_update(zucsv_infer *st, const unsigned char *str, size_t len) {
  st->all_missing = 0;

  if (st->can_logical && !zucsv_is_logical(str, len))
    st->can_logical = 0;
  if (st->can_integer && !zucsv_is_integer(str, len, NULL))
    st->can_integer = 0;
  if (st->can_double && !zucsv_is_double(str, len))
    st->can_double = 0;
}

zucsv_type zucsv_infer_result(const zucsv_infer *st) {
  /* A column with no evidence stays character: there is nothing to infer
     from, and guessing logical would be a trap (design SS11). */
  if (st->all_missing)
    return ZUCSV_CHARACTER;
  if (st->can_logical)
    return ZUCSV_LOGICAL;
  if (st->can_integer)
    return ZUCSV_INTEGER;
  if (st->can_double)
    return ZUCSV_DOUBLE;
  return ZUCSV_CHARACTER;
}
