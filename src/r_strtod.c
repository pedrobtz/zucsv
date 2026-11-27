/* read_csv()'s double converter mode (design decisions 15 and 19, #23).
 *
 * Chosen once at load by zucsv_strtod_probe() from inst/include/zucsv-r.h --
 * the same header, and the same call, a package linking libzucsv.a makes --
 * and passed to the core with every read. It is the one value here that is
 * written after load, and only by the test hook below. */

#include "rzucsv.h"

#include "zucsv-r.h"

int zucsv_r_strtod_mode = ZUCSV_STRTOD_UNSET;

void zucsv_r_strtod_init(void) {
  zucsv_r_strtod_mode = zucsv_strtod_probe();
}

/* Tests only (#36): returns the mode in use, after calibration, and with a
   non-NULL `mode` switches to it. 0 double, 1 long double, 2 R_strtod, 3 and
   4 the verbatim long double and double instances. Not exported. */
SEXP C_strtod_mode(SEXP mode) {
  if (zucsv_r_strtod_mode == ZUCSV_STRTOD_UNSET)
    zucsv_r_strtod_init();
  int old = zucsv_r_strtod_mode;
  if (mode != R_NilValue) {
    if (TYPEOF(mode) != INTSXP || XLENGTH(mode) != 1 || INTEGER(mode)[0] < 0 ||
        INTEGER(mode)[0] > ZUCSV_STRTOD_DOUBLE_VERBATIM)
      Rf_error("'mode' must be a single integer from 0 to 4");
    zucsv_r_strtod_mode = INTEGER(mode)[0];
  }
  return Rf_ScalarInteger(old);
}
