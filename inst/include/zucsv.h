/* zucsv -- read_csv() as an Arrow C stream, for other packages (#23).
 *
 * The R-free C API that a package reaches through `LinkingTo: zucsv`,
 * linked from the static archive libzucsv.a that zucsv installs in
 * <pkg>/lib${R_ARCH}. The archive holds zucsv's reader and the zsv parser
 * it vendors, with every symbol hidden; it builds its Arrow arrays with the
 * nanoarrow that zuarrow bundles, so a consumer links
 *
 *     libzucsv.a  then  libzuarrow.a
 *
 * and needs both packages only at build time (design SS30).
 *
 *   zucsv.h    this file: no R, only <stddef.h> and <stdint.h>.
 *   zucsv-r.h  the one piece that needs R: zucsv_strtod_probe(), which
 *              finds the double converter that agrees with R's own.
 *
 * Every function here may be called from any thread, R's main thread or
 * not, except that a stream whose strtod_mode is ZUCSV_STRTOD_CALL calls
 * the `strtod` it was given, so read it where that function may run. None
 * calls R, longjmps, aborts or writes to stdout or stderr: each returns a
 * code. The archive holds no mutable global, so distinct streams share
 * nothing; one stream is used from one thread at a time.
 *
 * Copyright (c) 2026 Pedro Baltazar. MIT licensed; see zucsv's LICENSE.
 */
#ifndef ZUCSV_PUBLIC_H
#define ZUCSV_PUBLIC_H

#include <stddef.h>
#include <stdint.h>

/* Changes when a declaration here changes meaning. A field appended to
   zucsv_options changes nothing: struct_size covers it. */
#define ZUCSV_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/* The Arrow C stream interface. Its definition comes from zuarrow.h (or
   any header that follows the Arrow specification); this file needs only
   the name. */
struct ArrowArrayStream;

/* header: what read_csv()'s `header = TRUE`, `FALSE` and `NA` mean. */
#define ZUCSV_HEADER_NO 0
#define ZUCSV_HEADER_YES 1
#define ZUCSV_HEADER_DETECT 2

/* The double converters (design SS11, decisions 5, 15 and 19). zucsv
   carries R's own decimal algorithm in two accumulator widths, each with
   and without its exact shortcuts; which one agrees with as.numeric()
   depends on how R was built, and R does not say. zucsv_strtod_probe() in
   zucsv-r.h asks R once and returns the mode to pass here. Without R there
   is nothing to agree with, so the mode is a required option: a stream
   never guesses it. */
#define ZUCSV_STRTOD_UNSET (-1)
#define ZUCSV_STRTOD_DOUBLE 0
#define ZUCSV_STRTOD_LDOUBLE 1
/* No instance agreed: every double cell is copied, terminated and given to
   options->strtod, which must then be R_strtod. */
#define ZUCSV_STRTOD_CALL 2
#define ZUCSV_STRTOD_LDOUBLE_VERBATIM 3
#define ZUCSV_STRTOD_DOUBLE_VERBATIM 4

/* What zucsv_open_stream() reads and how. struct_size comes first: set it
   to sizeof(zucsv_options) as you compiled it, which zucsv_options_init()
   does. Everything but strtod_mode mirrors an argument of read_csv(), with
   its default; there is no column selection (design SS3): drop columns
   from the stream instead. */
typedef struct zucsv_options {
  uint32_t struct_size;
  /* ZUCSV_HEADER_YES, _NO or _DETECT. */
  int32_t header;
  /* One ASCII byte, not a newline, carriage return, form feed or quote. */
  char delimiter;
  /* The na strings, n_na of them, as UTF-8; a cell equal to one is null.
     NULL with n_na 0 for none (read_csv()'s na = NULL). Copied at open. */
  const char *const *na;
  int64_t n_na;
  /* NULL to infer every column's type over the whole file; otherwise 1 or
     ncol of "logical", "integer", "double" or "character". Copied at
     open. */
  const char *const *col_types;
  int64_t n_col_types;
  /* Required: a ZUCSV_STRTOD_* mode from zucsv_strtod_probe(). */
  int32_t strtod_mode;
  /* With ZUCSV_STRTOD_CALL only: R_strtod. NULL otherwise. */
  double (*strtod)(const char *, char **);
  /* Rows in each batch but the last, from 1. */
  int64_t batch_rows;
} zucsv_options;

/* Sets struct_size and read_csv()'s defaults: header ZUCSV_HEADER_YES,
   delimiter ',', na "" and "NA", types inferred, 65536 rows a batch, and
   strtod_mode ZUCSV_STRTOD_UNSET, which zucsv_open_stream() refuses until
   it is set. */
void zucsv_options_init(zucsv_options *options);

/* Opens the CSV file at `path` as an ArrowArrayStream in `out`, which the
 * caller releases. `path` is passed to fopen() as it is.
 *
 * Opening is read_csv()'s pass 1: the whole file is read, validated and
 * its types inferred, so an error read_csv() would raise is returned here,
 * with the same message. The stream's schema is a struct (`+s`) of one
 * nullable child per column, named by the header (or V1, V2, ...): `b`
 * for logical, `i` for integer, `g` for double and `u` for character.
 * get_next() is pass 2: each batch holds the next batch_rows rows, a cell
 * equal to an na string is null, a double is the value as.numeric() gives
 * bit for bit, and every string is strict UTF-8. No row is null as a
 * whole. Batches own their memory and outlive the stream.
 *
 * Returns 0, or an errno code: EINVAL for an invalid option or a file
 * read_csv() would refuse, EIO for a file that cannot be opened or read,
 * ENOMEM. On failure `out` is still a stream, in an error state:
 * get_schema() and get_next() return the same code and get_last_error()
 * says why; release it like any other. Only when even that cannot be
 * allocated is ENOMEM returned with out->release NULL.
 *
 * A get_next() that fails (the file changed after it was opened, a read
 * error, ENOMEM) returns non-zero, leaves its array released, and the
 * stream stays failed: every later call returns the same code. */
int zucsv_open_stream(const char *path, const zucsv_options *options,
                      struct ArrowArrayStream *out);

/* The ZUCSV_ABI_VERSION the archive was built with. */
uint32_t zucsv_abi_version(void);

/* ---- for zucsv_strtod_probe() --------------------------------------- */

/* Non-zero when `mode` can be used on this machine: a fast instance's
   shortcuts assume its accumulator's full width at run time, which an
   emulator (valgrind) may not give. ZUCSV_STRTOD_CALL is always usable. */
int zucsv_strtod_usable(int32_t mode);

/* The value `mode` gives for the cell s[0..n), which must be double syntax
   as read_csv() accepts it. Not for ZUCSV_STRTOD_CALL. */
double zucsv_strtod_convert(int32_t mode, const char *s, size_t n);

/* The i-th probe value, from 0, or NULL past the last: values on which the
   instances disagree somewhere. */
const char *zucsv_strtod_probe_value(int i);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ZUCSV_PUBLIC_H */
