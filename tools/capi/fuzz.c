/* libFuzzer harness for zucsv's R-free core and stream (#23, design SS21).
 *
 * Each input is a CSV, read as text through zucsv_open_stream_text() with
 * the header given, absent and detected. The oracle, beyond "no crash":
 *
 *  - pass 2 agrees with pass 1: the rows and columns the stream delivers
 *    are the counts zucsv_core_pass1() found on its own;
 *  - the stream equals a second read: a second open gives the same code,
 *    the same message, or the same digest of every value, and so does a
 *    read in batches of 3 rows (the batch boundaries change nothing);
 *  - every batch keeps the contract: struct of children of equal length,
 *    no null row, formats b, i, g or u (capi.h).
 *
 * Built by r-actions' fuzz.yml (fuzz.yaml) with ASan and UBSan, and by
 * `make -C tools/capi replay` as a plain program over the seed corpus. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "capi.h"
#include "core.h"

static void check(int ok) {
  if (!ok)
    __builtin_trap();
}

static void read_once(const char *data, size_t size, int header, int64_t batch_rows,
                      capi_read *r) {
  zucsv_options o;
  zucsv_options_init(&o);
  o.strtod_mode = ZUCSV_STRTOD_DOUBLE_VERBATIM;
  o.header = header;
  o.batch_rows = batch_rows;
  struct ArrowArrayStream s;
  int code = zucsv_open_stream_text(data, size, &o, &s);
  capi_consume(&s, code, r);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  const char *csv = (const char *)data;
  const int headers[] = {ZUCSV_HEADER_YES, ZUCSV_HEADER_NO, ZUCSV_HEADER_DETECT};
  for (int h = 0; h < 3; h++) {
    capi_read a, b, c;
    read_once(csv, size, headers[h], ZUCSV_BATCH_ROWS, &a);
    read_once(csv, size, headers[h], ZUCSV_BATCH_ROWS, &b);
    read_once(csv, size, headers[h], 3, &c);
    /* never a broken batch, never an array left unreleased on failure */
    check(a.code >= 0 && c.code >= 0);
    check(a.code == b.code && a.code == c.code);
    check(strcmp(a.message, b.message) == 0 && strcmp(a.message, c.message) == 0);
    if (a.code != 0)
      continue;
    check(a.digest == b.digest && a.digest == c.digest);
    check(a.nrow == c.nrow && a.ncol == c.ncol && strcmp(a.formats, c.formats) == 0);
    check(c.max_batch <= 3);

    /* pass 1 on its own */
    zucsv_core core;
    zucsv_core_init(&core);
    core.data = csv;
    core.len = size;
    core.want_header = headers[h];
    core.strtod_mode = ZUCSV_STRTOD_DOUBLE_VERBATIM;
    static const char *na_str[] = {"", "NA"};
    static size_t na_len[] = {0, 2};
    core.na.str = na_str;
    core.na.len = na_len;
    core.na.n = 2;
    check(zucsv_core_pass1(&core) == 0);
    check(core.ncol == a.ncol && core.nrow == (double)a.nrow);
    zucsv_core_close(&core);
  }
  return 0;
}
