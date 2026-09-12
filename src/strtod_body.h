/* The decimal branch of R's R_strtod5() (src/main/util.c), transcribed with
 * the same order of operations but reading s[i..n) instead of a
 * NUL-terminated string, and with the accumulator type left open.
 *
 * R accumulates in LDOUBLE, which is `long double` when R was built with it
 * and plain `double` otherwise -- and R does not tell packages which. So
 * convert.c includes this file twice, once per type, and picks the instance
 * that agrees with R_strtod at run time. See zucsv_as_double().
 *
 * Define ZUCSV_ACC and ZUCSV_STRTOD_NAME before including; both are undefined
 * again at the end.
 *
 * `i` is the index just past any sign, and `sign` is +1/-1, both already
 * parsed by the caller, which has also dispatched Inf and NaN by name. */

static double ZUCSV_STRTOD_NAME(const unsigned char *s, size_t n, size_t i, int sign) {
  ZUCSV_ACC ans = 0.0;
  int k, expn = 0, ndigits = 0;

  for (; i < n && s[i] >= '0' && s[i] <= '9'; i++, ndigits++)
    ans = 10 * ans + (s[i] - '0');
  if (i < n && s[i] == '.')
    for (i++; i < n && s[i] >= '0' && s[i] <= '9'; i++, ndigits++, expn--)
      ans = 10 * ans + (s[i] - '0');

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
    for (k = -expn, fac = 1.0; k; k >>= 1, p10 *= p10)
      if (k & 1)
        fac *= p10;
    ans /= fac;
  } else if (ans != 0.0) { /* allow big exponents on 0, e.g. 0E4933 */
    for (k = expn, fac = 1.0; k; k >>= 1, p10 *= p10)
      if (k & 1)
        fac *= p10;
    ans *= fac;
  }

  /* explicit overflow to infinity */
  if (ans > DBL_MAX)
    return sign > 0 ? R_PosInf : R_NegInf;

  return sign * (double)ans;
}

#undef ZUCSV_ACC
#undef ZUCSV_STRTOD_NAME
