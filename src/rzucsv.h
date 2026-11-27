#ifndef ZUCSV_RZUCSV_H
#define ZUCSV_RZUCSV_H

/* The R side of the package: read_csv(), sniff_csv() and the test hooks,
   all clients of the R-free core in core.h. */

#define R_NO_REMAP
#include <R.h>
#include <Rinternals.h>

#include "core.h"

/* The strtod mode read_csv() runs in: zucsv_strtod_probe()'s answer, from
   R_init_zucsv, as every package linking libzucsv.a gets it (r_strtod.c). */
extern int zucsv_r_strtod_mode;
void zucsv_r_strtod_init(void);

SEXP C_read_csv(SEXP file, SEXP text, SEXP header, SEXP delimiter, SEXP na, SEXP col_types);
SEXP C_sniff_csv(SEXP file, SEXP text, SEXP delimiter, SEXP na);
/* Tests only, never exported (#36): switches the converter instance. */
SEXP C_strtod_mode(SEXP mode);
/* Tests only (#23): the Arrow stream, read back in C (r_stream.c). */
SEXP C_stream_read(SEXP file, SEXP text, SEXP header, SEXP delimiter, SEXP na, SEXP col_types,
                   SEXP batch_rows, SEXP strtod_mode);
SEXP C_stream_xptr(SEXP file, SEXP header, SEXP delimiter, SEXP na, SEXP col_types);
SEXP C_abi_version(void);

#endif /* ZUCSV_RZUCSV_H */
