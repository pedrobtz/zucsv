/* Cell-level predicates and conversions (design SS11-SS13).
 *
 * Everything here works on a {pointer, length} cell borrowed from the
 * parser's buffer: nothing is copied, and the cell is never modified. */

#include "zucsv.h"

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
