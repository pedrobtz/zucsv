/* The decimal branch of R's R_strtod5() (src/main/util.c), transcribed with
 * the same order of operations but reading s[i..n) instead of a
 * NUL-terminated string, and with the accumulator type left open.
 *
 * R accumulates in LDOUBLE, which is `long double` when R was built with it
 * and plain `double` otherwise -- and R does not tell packages which. So
 * convert.c includes this file twice, once per type, and picks the instance
 * that agrees with R_strtod at run time. See zucsv_as_double().
 *
 * Define ZUCSV_ACC, ZUCSV_ACC_MANT_DIG and ZUCSV_STRTOD_NAME before
 * including; all are undefined again at the end. Define
 * ZUCSV_STRTOD_NO_FASTPATH to get R's loops verbatim, with no shortcuts.
 * convert.c builds both, and tests/testthat/test-strtod.R selects each
 * through C_strtod_mode() and requires them to agree bit for bit (#36).
 *
 * `i` is the index just past any sign, and `sign` is +1/-1, both already
 * parsed by the caller, which has also dispatched Inf and NaN by name.
 *
 * Two shortcuts, each identical to R's arithmetic by construction:
 *
 *  - Digits. R's `ans = 10*ans + d` never rounds while the running value is
 *    an integer below 2^MANT_DIG, so up to that point accumulating in a
 *    uint64_t and converting once gives the same bits. With a 64-bit
 *    mantissa that is every input of at most 19 digits (no uint64 wrap);
 *    with 53 bits, values up to 2^53.
 *  - Powers of ten. R builds 10^k by repeated squaring in the accumulator
 *    type. The result is exactly 10^k while 5^k fits the mantissa -- k <= 27
 *    for 64 bits, k <= 22 for 53 -- so a table filled by that very loop is
 *    the same value without the loop. */

#ifndef ZUCSV_CAT
#define ZUCSV_CAT_(a, b) a##b
#define ZUCSV_CAT(a, b) ZUCSV_CAT_(a, b)
#endif

#ifdef ZUCSV_STRTOD_NO_FASTPATH
#define ZUCSV_FASTPATH 0
#else
#define ZUCSV_FASTPATH 1
#endif

#if ZUCSV_ACC_MANT_DIG >= 64
#define ZUCSV_ACC_FITS(m) 1 /* any uint64 is exact in a 64-bit mantissa */
#define ZUCSV_ACC_EXACT_POW 27
#else
#define ZUCSV_ACC_FITS(m) ((m) <= ((uint64_t)1 << ZUCSV_ACC_MANT_DIG))
#define ZUCSV_ACC_EXACT_POW 22
#endif

#define ZUCSV_POW_NAME ZUCSV_CAT(ZUCSV_STRTOD_NAME, _pow10)
#define ZUCSV_TBL_NAME ZUCSV_CAT(ZUCSV_STRTOD_NAME, _tbl)
#define ZUCSV_TABLE_OK_NAME ZUCSV_CAT(ZUCSV_STRTOD_NAME, _table_ok)

/* R's power loop, verbatim; also what the table is checked against. */
static ZUCSV_ACC ZUCSV_POW_NAME(int k) {
  ZUCSV_ACC p10 = 10., fac = 1.0;
  for (; k; k >>= 1, p10 *= p10)
    if (k & 1)
      fac *= p10;
  return fac;
}

/* The exact powers 10^0 .. 10^ZUCSV_ACC_EXACT_POW, as constants. Each is
   exactly representable in the accumulator (5^k fits its mantissa), so the
   literal is the value R's loop computes; ZUCSV_TABLE_OK_NAME() checks that
   on the machine at hand, and zucsv_strtod_usable() refuses the fast
   instance where it fails. Constant rather than filled at load (#23): the
   archive holds no mutable global, and nothing has to run before a read. */
#ifndef ZUCSV_STRTOD_NO_FASTPATH
static const ZUCSV_ACC ZUCSV_TBL_NAME[] = {
  1e0L,  1e1L,  1e2L,  1e3L,  1e4L,  1e5L,  1e6L,  1e7L,  1e8L,  1e9L,
  1e10L, 1e11L, 1e12L, 1e13L, 1e14L, 1e15L, 1e16L, 1e17L, 1e18L, 1e19L,
  1e20L, 1e21L, 1e22L
#if ZUCSV_ACC_EXACT_POW > 22
  , 1e23L, 1e24L, 1e25L, 1e26L, 1e27L
#endif
};
#define ZUCSV_POW_FAST(k) (k <= ZUCSV_ACC_EXACT_POW ? ZUCSV_TBL_NAME[k] : ZUCSV_POW_NAME(k))

static int ZUCSV_TABLE_OK_NAME(void) {
  for (int k = 0; k <= ZUCSV_ACC_EXACT_POW; k++) {
    volatile ZUCSV_ACC want = ZUCSV_POW_NAME(k);
    if (want != ZUCSV_TBL_NAME[k])
      return 0;
  }
  return 1;
}
#else
#define ZUCSV_POW_FAST(k) ZUCSV_POW_NAME(k)
#endif

static double ZUCSV_STRTOD_NAME(const unsigned char *s, size_t n, size_t i, int sign) {
  ZUCSV_ACC ans = 0.0;
  int k, expn = 0, ndigits = 0;
  const size_t digits_start = i;
  uint64_t m = 0;

  for (; i < n && s[i] >= '0' && s[i] <= '9'; i++, ndigits++)
    m = 10 * m + (s[i] - '0');
  if (i < n && s[i] == '.')
    for (i++; i < n && s[i] >= '0' && s[i] <= '9'; i++, ndigits++, expn--)
      m = 10 * m + (s[i] - '0');

  if (ZUCSV_FASTPATH && ndigits <= 19 && ZUCSV_ACC_FITS(m)) {
    ans = (ZUCSV_ACC)m;
  } else {
    /* R's loop, verbatim */
    size_t j = digits_start;
    for (; j < n && s[j] >= '0' && s[j] <= '9'; j++)
      ans = 10 * ans + (s[j] - '0');
    if (j < n && s[j] == '.')
      for (j++; j < n && s[j] >= '0' && s[j] <= '9'; j++)
        ans = 10 * ans + (s[j] - '0');
  }

  if (i < n && (s[i] == 'e' || s[i] == 'E')) {
    int expsign = 1;
    i++;
    if (i < n && s[i] == '-') {
      expsign = -1;
      i++;
    } else if (i < n && s[i] == '+') {
      i++;
    }
    /* R caps the exponent prefix at 9999 (MAX_EXPONENT_PREFIX, PR#16358);
       over/underflow below then does the rest. */
    for (k = 0; i < n && s[i] >= '0' && s[i] <= '9'; i++)
      k = (k < 9999) ? k * 10 + (s[i] - '0') : k;
    expn += expsign * k;
  }

  /* avoid unnecessary underflow for large negative exponents */
  if (expn + ndigits < -300) {
    for (k = 0; k < ndigits; k++)
      ans /= 10.0;
    expn += ndigits;
  }

  ZUCSV_ACC p10 = 10., fac = 1.0;
  if (expn < -307) { /* use underflow, not overflow */
    for (k = -expn, fac = 1.0; k; k >>= 1, p10 *= p10)
      if (k & 1)
        fac /= p10;
    ans *= fac;
  } else if (expn < 0) { /* positive powers are exact */
    k = -expn;
    fac = ZUCSV_POW_FAST(k);
    ans /= fac;
  } else if (ans != 0.0) { /* allow big exponents on 0, e.g. 0E4933 */
    k = expn;
    fac = ZUCSV_POW_FAST(k);
    ans *= fac;
  }

  /* explicit overflow to infinity */
  if (ans > DBL_MAX)
    return sign > 0 ? INFINITY : -INFINITY;

  return sign * (double)ans;
}

#undef ZUCSV_FASTPATH
#undef ZUCSV_ACC_FITS
#undef ZUCSV_ACC_EXACT_POW
#undef ZUCSV_POW_NAME
#undef ZUCSV_TBL_NAME
#undef ZUCSV_POW_FAST
#undef ZUCSV_TABLE_OK_NAME
#undef ZUCSV_ACC
#undef ZUCSV_ACC_MANT_DIG
#undef ZUCSV_STRTOD_NAME
