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
 * ZUCSV_STRTOD_NO_FASTPATH to get R's loops verbatim, with no shortcuts --
 * the test harness compiles both and requires them to agree bit for bit.
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
#define ZUCSV_FILL_NAME ZUCSV_CAT(ZUCSV_STRTOD_NAME, _fill)

/* R's power loop, verbatim; also what fills the table. */
static ZUCSV_ACC ZUCSV_POW_NAME(int k) {
  ZUCSV_ACC p10 = 10., fac = 1.0;
  for (; k; k >>= 1, p10 *= p10)
    if (k & 1)
      fac *= p10;
  return fac;
}

/* Filled once by zucsv_numeric_init() from R_init_zucsv(), then read-only.
   Not lazily initialised: a mutable static written on first use would be a
   data race the moment conversion runs on worker threads, and it costs a
   branch in the hot path besides. */
static ZUCSV_ACC ZUCSV_TBL_NAME[ZUCSV_ACC_EXACT_POW + 1];

static void ZUCSV_FILL_NAME(void) {
  for (int k = 0; k <= ZUCSV_ACC_EXACT_POW; k++)
    ZUCSV_TBL_NAME[k] = ZUCSV_POW_NAME(k);
}

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
    fac = (ZUCSV_FASTPATH && k <= ZUCSV_ACC_EXACT_POW) ? ZUCSV_TBL_NAME[k] : ZUCSV_POW_NAME(k);
    ans /= fac;
  } else if (ans != 0.0) { /* allow big exponents on 0, e.g. 0E4933 */
    k = expn;
    fac = (ZUCSV_FASTPATH && k <= ZUCSV_ACC_EXACT_POW) ? ZUCSV_TBL_NAME[k] : ZUCSV_POW_NAME(k);
    ans *= fac;
  }

  /* explicit overflow to infinity */
  if (ans > DBL_MAX)
    return sign > 0 ? R_PosInf : R_NegInf;

  return sign * (double)ans;
}

#undef ZUCSV_FASTPATH
#undef ZUCSV_ACC_FITS
#undef ZUCSV_ACC_EXACT_POW
#undef ZUCSV_POW_NAME
#undef ZUCSV_TBL_NAME
#undef ZUCSV_FILL_NAME
#undef ZUCSV_ACC
#undef ZUCSV_ACC_MANT_DIG
#undef ZUCSV_STRTOD_NAME
