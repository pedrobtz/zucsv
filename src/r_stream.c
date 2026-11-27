/* Tests only (#23): the Arrow stream as a package that links libzucsv.a
 * sees it, read back here so the suite can hold it to read_csv().
 *
 * C_stream_read() opens a stream through the public options, reads every
 * batch and returns list(code, message, names, formats, batches, columns):
 * the open's or the failing get_next()'s code and message (the schema's
 * format when there is no failure), the schema's names and formats, and
 * each batch as a list of its columns, its length as attribute "rows". The stream is released exactly once, inside
 * R_UnwindProtect, however the call ends.
 *
 * C_stream_xptr() hands the stream to R as a "nanoarrow_array_stream"
 * external pointer, for zuarrow::arrow_to_r() to validate and convert. */

#include "rzucsv.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "zuarrow.h"

typedef struct {
  struct ArrowArrayStream stream;
  struct ArrowSchema schema;
  struct ArrowArray array;
  zucsv_options opts;
  const char **na;
  const char **types;
  const char *path;
  const char *text;
  size_t text_len;
  int code;
  const char *message;
} zucsv_reader_t;

static void zucsv_reader_cleanup(void *data, Rboolean jump) {
  (void)jump;
  zucsv_reader_t *r = (zucsv_reader_t *)data;
  if (r->array.release)
    r->array.release(&r->array);
  if (r->schema.release)
    r->schema.release(&r->schema);
  if (r->stream.release)
    r->stream.release(&r->stream);
}

static int zucsv_bit(const void *buf, int64_t i) {
  return (((const uint8_t *)buf)[i >> 3] >> (i & 7)) & 1;
}

/* One batch's column j into `col` from row `at` on. */
static void zucsv_copy_column(SEXP col, const char *format, const struct ArrowArray *a,
                              R_xlen_t at) {
  const void *valid = a->n_buffers > 0 ? a->buffers[0] : NULL;
  int64_t off = a->offset;
  for (int64_t i = 0; i < a->length; i++) {
    int isnull = valid != NULL && !zucsv_bit(valid, off + i);
    R_xlen_t k = at + (R_xlen_t)i;
    switch (format[0]) {
    case 'b':
      LOGICAL(col)[k] = isnull ? NA_LOGICAL : zucsv_bit(a->buffers[1], off + i);
      break;
    case 'i':
      INTEGER(col)[k] = isnull ? NA_INTEGER : ((const int32_t *)a->buffers[1])[off + i];
      break;
    case 'g':
      REAL(col)[k] = isnull ? NA_REAL : ((const double *)a->buffers[1])[off + i];
      break;
    default: {
      if (isnull) {
        SET_STRING_ELT(col, k, NA_STRING);
        break;
      }
      const int32_t *offsets = (const int32_t *)a->buffers[1];
      const char *bytes = (const char *)a->buffers[2];
      int32_t from = offsets[off + i], to = offsets[off + i + 1];
      SET_STRING_ELT(col, k, Rf_mkCharLenCE(bytes + from, (int)(to - from), CE_UTF8));
      break;
    }
    }
  }
}

static SEXPTYPE zucsv_sexptype(const char *format) {
  switch (format[0]) {
  case 'b':
    return LGLSXP;
  case 'i':
    return INTSXP;
  case 'g':
    return REALSXP;
  default:
    return STRSXP;
  }
}

static SEXP zucsv_reader_body(void *data) {
  zucsv_reader_t *r = (zucsv_reader_t *)data;
  int code = r->path ? zucsv_open_stream(r->path, &r->opts, &r->stream)
                     : zucsv_open_stream_text(r->text, r->text_len, &r->opts, &r->stream);

  SEXP out = PROTECT(Rf_allocVector(VECSXP, 6));
  SEXP tags = PROTECT(Rf_allocVector(STRSXP, 6));
  const char *fields[] = {"code", "message", "names", "formats", "batches", "schema_flags"};
  for (int i = 0; i < 6; i++)
    SET_STRING_ELT(tags, i, Rf_mkChar(fields[i]));
  Rf_setAttrib(out, R_NamesSymbol, tags);

  if (code == 0)
    code = r->stream.get_schema(&r->stream, &r->schema);
  if (code != 0) {
    const char *msg = r->stream.release ? r->stream.get_last_error(&r->stream) : NULL;
    SET_VECTOR_ELT(out, 0, Rf_ScalarInteger(code));
    SET_VECTOR_ELT(out, 1, Rf_mkString(msg ? msg : ""));
    UNPROTECT(2);
    return out;
  }

  int64_t ncol = r->schema.n_children;
  SEXP names = PROTECT(Rf_allocVector(STRSXP, (R_xlen_t)ncol));
  SEXP formats = PROTECT(Rf_allocVector(STRSXP, (R_xlen_t)ncol));
  for (int64_t j = 0; j < ncol; j++) {
    SET_STRING_ELT(names, j, Rf_mkCharCE(r->schema.children[j]->name, CE_UTF8));
    SET_STRING_ELT(formats, j, Rf_mkChar(r->schema.children[j]->format));
  }
  SET_VECTOR_ELT(out, 2, names);
  SET_VECTOR_ELT(out, 3, formats);
  SET_VECTOR_ELT(out, 1, Rf_mkString(r->schema.format));
  {
    SEXP flags = PROTECT(Rf_allocVector(REALSXP, (R_xlen_t)ncol + 1));
    REAL(flags)[0] = (double)r->schema.flags;
    for (int64_t j = 0; j < ncol; j++)
      REAL(flags)[j + 1] = (double)r->schema.children[j]->flags;
    SET_VECTOR_ELT(out, 5, flags);
    UNPROTECT(1);
  }

  /* Batches are converted one at a time into growing column lists; the
     lists are joined in R. */
  SEXP batches = PROTECT(Rf_allocVector(VECSXP, 0));
  R_xlen_t nb = 0;
  for (;;) {
    code = r->stream.get_next(&r->stream, &r->array);
    if (code != 0) {
      const char *msg = r->stream.get_last_error(&r->stream);
      SET_VECTOR_ELT(out, 1, Rf_mkString(msg ? msg : ""));
      break;
    }
    if (!r->array.release)
      break;
    if (r->array.n_children != ncol || r->array.null_count != 0)
      Rf_error("batch %d has %d children and %d null rows", (int)nb + 1,
               (int)r->array.n_children, (int)r->array.null_count);
    SEXP cols = PROTECT(Rf_allocVector(VECSXP, (R_xlen_t)ncol));
    for (int64_t j = 0; j < ncol; j++) {
      const struct ArrowArray *a = r->array.children[j];
      if (a->length != r->array.length)
        Rf_error("batch %d: column %d has %d rows, the batch %d", (int)nb + 1, (int)j + 1,
                 (int)a->length, (int)r->array.length);
      SEXP col = PROTECT(Rf_allocVector(zucsv_sexptype(r->schema.children[j]->format),
                                        (R_xlen_t)a->length));
      zucsv_copy_column(col, r->schema.children[j]->format, a, 0);
      SET_VECTOR_ELT(cols, j, col);
      UNPROTECT(1);
    }
    Rf_setAttrib(cols, Rf_install("rows"), PROTECT(Rf_ScalarReal((double)r->array.length)));
    UNPROTECT(1);
    SEXP grown = PROTECT(Rf_allocVector(VECSXP, nb + 1));
    for (R_xlen_t i = 0; i < nb; i++)
      SET_VECTOR_ELT(grown, i, VECTOR_ELT(batches, i));
    SET_VECTOR_ELT(grown, nb, cols);
    UNPROTECT(3); /* grown, cols, batches */
    batches = PROTECT(grown);
    nb++;
    r->array.release(&r->array);
    r->array.release = NULL;
  }
  SET_VECTOR_ELT(out, 0, Rf_ScalarInteger(code));
  SET_VECTOR_ELT(out, 4, batches);
  UNPROTECT(5);
  return out;
}

static void zucsv_r_options(zucsv_reader_t *r, SEXP header, SEXP delimiter, SEXP na,
                            SEXP col_types, SEXP batch_rows, SEXP strtod_mode) {
  zucsv_options_init(&r->opts);
  int h = LOGICAL(header)[0];
  r->opts.header = h == NA_LOGICAL ? ZUCSV_HEADER_DETECT : h ? ZUCSV_HEADER_YES : ZUCSV_HEADER_NO;
  r->opts.delimiter = CHAR(STRING_ELT(delimiter, 0))[0];
  if (na == R_NilValue) {
    r->opts.na = NULL;
    r->opts.n_na = 0;
  } else {
    R_xlen_t n = XLENGTH(na);
    r->na = (const char **)R_alloc((size_t)(n ? n : 1), sizeof(char *));
    for (R_xlen_t i = 0; i < n; i++)
      r->na[i] = Rf_translateCharUTF8(STRING_ELT(na, i));
    r->opts.na = r->na;
    r->opts.n_na = (int64_t)n;
  }
  if (col_types != R_NilValue) {
    R_xlen_t n = XLENGTH(col_types);
    r->types = (const char **)R_alloc((size_t)(n ? n : 1), sizeof(char *));
    for (R_xlen_t i = 0; i < n; i++)
      r->types[i] = CHAR(STRING_ELT(col_types, i));
    r->opts.col_types = r->types;
    r->opts.n_col_types = (int64_t)n;
  }
  if (batch_rows != R_NilValue)
    r->opts.batch_rows = (int64_t)Rf_asReal(batch_rows);
  int mode = strtod_mode == R_NilValue ? zucsv_r_strtod_mode : Rf_asInteger(strtod_mode);
  r->opts.strtod_mode = mode;
  r->opts.strtod = mode == ZUCSV_STRTOD_CALL ? R_strtod : NULL;
}

SEXP C_stream_read(SEXP file, SEXP text, SEXP header, SEXP delimiter, SEXP na, SEXP col_types,
                   SEXP batch_rows, SEXP strtod_mode) {
  zucsv_reader_t r;
  memset(&r, 0, sizeof(r));
  if (file != R_NilValue) {
    r.path = Rf_translateChar(STRING_ELT(file, 0));
  } else {
    r.text = Rf_translateCharUTF8(STRING_ELT(text, 0));
    r.text_len = strlen(r.text);
  }
  zucsv_r_options(&r, header, delimiter, na, col_types, batch_rows, strtod_mode);
  SEXP cont = PROTECT(R_MakeUnwindCont());
  SEXP out = R_UnwindProtect(zucsv_reader_body, &r, zucsv_reader_cleanup, &r, cont);
  UNPROTECT(1);
  return out;
}

static void zucsv_xptr_finalize(SEXP x) {
  struct ArrowArrayStream *s = (struct ArrowArrayStream *)R_ExternalPtrAddr(x);
  if (!s)
    return;
  if (s->release)
    s->release(s);
  free(s);
  R_ClearExternalPtr(x);
}

SEXP C_stream_xptr(SEXP file, SEXP header, SEXP delimiter, SEXP na, SEXP col_types) {
  zucsv_reader_t r;
  memset(&r, 0, sizeof(r));
  zucsv_r_options(&r, header, delimiter, na, col_types, R_NilValue, R_NilValue);
  struct ArrowArrayStream *s = (struct ArrowArrayStream *)calloc(1, sizeof(*s));
  if (!s)
    Rf_error("out of memory");
  SEXP x = PROTECT(R_MakeExternalPtr(s, R_NilValue, R_NilValue));
  R_RegisterCFinalizerEx(x, zucsv_xptr_finalize, TRUE);
  Rf_setAttrib(x, R_ClassSymbol, Rf_mkString("nanoarrow_array_stream"));
  /* Nothing below can raise: the stream is the pointer's from here on. */
  zucsv_open_stream(Rf_translateChar(STRING_ELT(file, 0)), &r.opts, s);
  UNPROTECT(1);
  return x;
}

SEXP C_abi_version(void) {
  SEXP out = PROTECT(Rf_allocVector(INTSXP, 2));
  INTEGER(out)[0] = ZUCSV_ABI_VERSION;
  INTEGER(out)[1] = (int)zucsv_abi_version();
  UNPROTECT(1);
  return out;
}
