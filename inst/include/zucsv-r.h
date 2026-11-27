/* zucsv's R-facing linkage header (#23).
 *
 * Include this instead of zucsv.h in a package that reads CSV through
 * libzucsv.a. It pulls in zucsv.h and adds the one piece that needs R:
 * finding which of zucsv's double converters agrees with R's own, so that
 * the stream's doubles are as.numeric()'s bit for bit (design SS11,
 * decision 15). R is not linked into the archive and is not asked lazily,
 * so the answer is a required option, zucsv_options.strtod_mode.
 *
 * Usage, once, on R's main thread (R_init_<pkg> is the place):
 *
 *     #include <zucsv-r.h>
 *
 *     static int32_t strtod_mode;
 *     void R_init_mypkg(DllInfo *dll) {
 *       ...
 *       strtod_mode = zucsv_strtod_probe();
 *     }
 *
 * and for each read, on any thread:
 *
 *     zucsv_options opts;
 *     zucsv_options_init(&opts);
 *     zucsv_options_set_strtod(&opts, strtod_mode);
 *     int code = zucsv_open_stream(path, &opts, &stream);
 *
 * zucsv's own read_csv() is set up the same way, from R_init_zucsv.
 */
#ifndef ZUCSV_R_H
#define ZUCSV_R_H

#include <string.h>

#include <R_ext/Utils.h> /* R_strtod */

#include "zucsv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The first converter that agrees with R_strtod() bit for bit on every
   probe value, in order of speed; ZUCSV_STRTOD_CALL when none does, which
   no build has been seen to need. Calls R: R's main thread only. The
   result is a plain value, to keep and hand to any thread.

   `static inline`, so a translation unit that includes this header and
   never calls it gets no unused-function warning. */
static inline int32_t zucsv_strtod_probe(void) {
  static const int32_t candidates[] = {
      ZUCSV_STRTOD_LDOUBLE, ZUCSV_STRTOD_LDOUBLE_VERBATIM, ZUCSV_STRTOD_DOUBLE,
      ZUCSV_STRTOD_DOUBLE_VERBATIM};
  for (size_t c = 0; c < sizeof(candidates) / sizeof(*candidates); c++) {
    int32_t mode = candidates[c];
    if (!zucsv_strtod_usable(mode))
      continue;
    int ok = 1;
    const char *p;
    for (int i = 0; ok && (p = zucsv_strtod_probe_value(i)) != NULL; i++) {
      char *end;
      double want = R_strtod(p, &end);
      double got = zucsv_strtod_convert(mode, p, strlen(p));
      ok = memcmp(&want, &got, sizeof want) == 0;
    }
    if (ok)
      return mode;
  }
  return ZUCSV_STRTOD_CALL;
}

/* Puts a mode from zucsv_strtod_probe() in `options`, with R_strtod as the
   converter when the mode is ZUCSV_STRTOD_CALL. A stream in that mode
   calls R_strtod for each double cell, so read it on R's main thread. */
static inline void zucsv_options_set_strtod(zucsv_options *options, int32_t mode) {
  options->strtod_mode = mode;
  options->strtod = mode == ZUCSV_STRTOD_CALL ? R_strtod : NULL;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ZUCSV_R_H */
