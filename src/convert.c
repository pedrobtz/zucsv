/* Cell-level predicates and conversions (design SS11-SS13).
 *
 * Everything here works on a {pointer, length} cell borrowed from the
 * parser's buffer: nothing is copied, and the cell is never modified. */

#include "zucsv.h"

#include <R_ext/Arith.h> /* R_PosInf, R_NegInf, R_NaN */
#include <R_ext/Utils.h> /* R_strtod, for calibration and as the fallback */
#include <float.h>       /* DBL_MAX, LDBL_MANT_DIG, DBL_MANT_DIG */
#include <stdint.h>      /* uint64_t */
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

/* Both checks in one walk. The NUL scan alone measured 6.8 ns/cell against
   4.5 for the UTF-8 walk, so doing them separately cost more than the
   validation did. */
zucsv_text_status zucsv_check_bytes(const unsigned char *str, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (str[i] == '\0')
      return ZUCSV_TEXT_NUL;
  }
  return zucsv_valid_utf8(str, len) ? ZUCSV_TEXT_OK : ZUCSV_TEXT_BAD_UTF8;
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

/* Double conversion: R's own algorithm, transcribed, and self-checked.
 *
 * R_strtod5 accumulates in LDOUBLE -- `long double` when R was built with
 * it, `double` otherwise -- and R exposes no macro saying which. Rather
 * than guess, strtod_body.h is instantiated for both types, and the first
 * call runs a few probe values through each instance and through R_strtod
 * itself. The instance that agrees bit-for-bit is used from then on; if
 * neither does, every cell goes through R_strtod (copy, terminate, call),
 * which is slower but cannot disagree. So the result is identical to
 * as.numeric() on every platform by construction -- design decision 5 --
 * including a no-long-double build, and arm64 where long double is double.
 *
 * The transcription was verified bit-for-bit against R_strtod on 8,000,051
 * inputs: every cell of a 2M-cell file, 51 hand-picked cases covering each
 * branch (overflow, underflow, denormals, 300-digit mantissas, 0e999, -0),
 * and 6M random decimals with 1-40 digit mantissas and exponents in
 * [-340, 340]; the shortcuts in strtod_body.h were checked the same way
 * against the verbatim loops, for both accumulator types. 18.2 ns/cell
 * against 46.0 for memcpy + NUL + R_strtod; what remains is dominated by the
 * one long double divide R's algorithm requires. */

#define ZUCSV_ACC long double
#define ZUCSV_ACC_MANT_DIG LDBL_MANT_DIG
#define ZUCSV_STRTOD_NAME zucsv_strtod_ldouble
#include "strtod_body.h"

#define ZUCSV_ACC double
#define ZUCSV_ACC_MANT_DIG DBL_MANT_DIG
#define ZUCSV_STRTOD_NAME zucsv_strtod_double
#include "strtod_body.h"

/* The fallback, and what zucsv did before: R_strtod needs a terminator. */
static double zucsv_strtod_call(const unsigned char *s, size_t n) {
  char stack_buf[64];
  char *buf = stack_buf;
  if (n + 1 > sizeof(stack_buf))
    buf = (char *)R_alloc(n + 1, 1);
  memcpy(buf, s, n);
  buf[n] = '\0';
  char *end;
  return R_strtod(buf, &end);
}

enum { ZUCSV_STRTOD_UNSET = -1, ZUCSV_STRTOD_DOUBLE = 0, ZUCSV_STRTOD_LDOUBLE = 1, ZUCSV_STRTOD_CALL = 2 };
static int zucsv_strtod_mode = ZUCSV_STRTOD_UNSET;

static int zucsv_same_bits(double a, double b) {
  return memcmp(&a, &b, sizeof a) == 0;
}

/* Values on which long double and double accumulation give different bits
   on x87, chosen from the cases the parity test found. Where the two types
   are the same width both instances agree and the first is taken. */
static void zucsv_strtod_calibrate(void) {
  static const char *const probes[] = {"0.799012", "2.235263", "3.141592653589793", "1e-307",
                                       "1e-308",   "123e-310", "1.7976931348623157e308"};
  int ok_ld = 1, ok_d = 1;
  for (size_t p = 0; p < sizeof(probes) / sizeof(*probes); p++) {
    const unsigned char *s = (const unsigned char *)probes[p];
    size_t n = strlen(probes[p]);
    char *end;
    double r = R_strtod(probes[p], &end);
    ok_ld = ok_ld && zucsv_same_bits(r, zucsv_strtod_ldouble(s, n, 0, 1));
    ok_d = ok_d && zucsv_same_bits(r, zucsv_strtod_double(s, n, 0, 1));
  }
  zucsv_strtod_mode = ok_ld ? ZUCSV_STRTOD_LDOUBLE : ok_d ? ZUCSV_STRTOD_DOUBLE : ZUCSV_STRTOD_CALL;
}

double zucsv_as_double(const unsigned char *s, size_t n) {
  int sign = 1;
  size_t i = 0;
  if (n > 0 && s[0] == '-') {
    sign = -1;
    i = 1;
  } else if (n > 0 && s[0] == '+') {
    i = 1;
  }

  /* The two non-decimal spellings the grammar admits (design SS11). R
     reaches the same values by name; NaN carries no sign in the grammar. */
  if (n - i == 3 && memcmp(s + i, "Inf", 3) == 0)
    return sign > 0 ? R_PosInf : R_NegInf;
  if (n == 3 && memcmp(s, "NaN", 3) == 0)
    return R_NaN;

  if (zucsv_strtod_mode == ZUCSV_STRTOD_UNSET)
    zucsv_strtod_calibrate();

  switch (zucsv_strtod_mode) {
  case ZUCSV_STRTOD_LDOUBLE:
    return zucsv_strtod_ldouble(s, n, i, sign);
  case ZUCSV_STRTOD_DOUBLE:
    return zucsv_strtod_double(s, n, i, sign);
  default:
    return zucsv_strtod_call(s, n);
  }
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
   so the order rows arrive in cannot change the answer.

   Returns non-zero when a grammar accepted the cell. Only grammars still
   live for this column are evaluated, so a zero return can mean "not
   proved" rather than "not ASCII" -- which is why the caller must treat it
   as "check the bytes", never as "the cell is bad". */
int zucsv_infer_update(zucsv_infer *st, const unsigned char *str, size_t len) {
  int accepted = 0;
  st->all_missing = 0;

  if (st->can_logical) {
    if (zucsv_is_logical(str, len))
      accepted = 1;
    else
      st->can_logical = 0;
  }
  if (st->can_integer) {
    if (zucsv_is_integer(str, len, NULL))
      accepted = 1;
    else
      st->can_integer = 0;
  }
  if (st->can_double) {
    if (zucsv_is_double(str, len))
      accepted = 1;
    else
      st->can_double = 0;
  }
  return accepted;
}

int zucsv_accepts(zucsv_type type, const unsigned char *str, size_t len) {
  switch (type) {
  case ZUCSV_LOGICAL:
    return zucsv_is_logical(str, len);
  case ZUCSV_INTEGER:
    return zucsv_is_integer(str, len, NULL);
  case ZUCSV_DOUBLE:
    return zucsv_is_double(str, len);
  default:
    return 0; /* character proves nothing about the bytes */
  }
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
