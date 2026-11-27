/* A package consuming zucsv's C API as zugodb will (#23): headers from
 * LinkingTo, libzucsv.a and libzuarrow.a from ./configure, the double
 * converter probed once at load on R's main thread, and the stream opened
 * and read on a thread that is not R's. Nothing here calls zucsv's or
 * zuarrow's R code, and nothing in the worker thread calls R. */

#define R_NO_REMAP
#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>
#include <R_ext/Visibility.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <zuarrow.h> /* the Arrow C data and stream structs */
#include <zucsv-r.h> /* zucsv.h and the probe */

static int32_t probed_mode = ZUCSV_STRTOD_UNSET;

/* What the worker hands back: every batch, still owned by us. */
typedef struct {
  const char *path;
  zucsv_options opts;
  struct ArrowArrayStream stream;
  struct ArrowSchema schema;
  struct ArrowArray *batches;
  int64_t nbatch;
  int code;
  char message[1024];
} job_t;

static void job_free(job_t *job) {
  for (int64_t b = 0; b < job->nbatch; b++)
    if (job->batches[b].release)
      job->batches[b].release(&job->batches[b]);
  free(job->batches);
  job->batches = NULL;
  job->nbatch = 0;
  if (job->schema.release)
    job->schema.release(&job->schema);
  if (job->stream.release)
    job->stream.release(&job->stream);
}

/* The whole read: no R in here, so it may run on any thread. */
static void job_run(job_t *job) {
  job->code = zucsv_open_stream(job->path, &job->opts, &job->stream);
  if (job->code == 0)
    job->code = job->stream.get_schema(&job->stream, &job->schema);
  int64_t cap = 0;
  while (job->code == 0) {
    if (job->nbatch == cap) {
      int64_t grown = cap ? 2 * cap : 8;
      struct ArrowArray *b = realloc(job->batches, (size_t)grown * sizeof(*b));
      if (!b) {
        job->code = ENOMEM;
        break;
      }
      job->batches = b;
      cap = grown;
    }
    struct ArrowArray *a = &job->batches[job->nbatch];
    a->release = NULL;
    job->code = job->stream.get_next(&job->stream, a);
    if (job->code != 0 || a->release == NULL)
      break;
    job->nbatch++;
  }
  if (job->code != 0 && job->stream.release) {
    const char *m = job->stream.get_last_error(&job->stream);
    snprintf(job->message, sizeof(job->message), "%s", m ? m : "");
  }
}

#ifdef _WIN32
static unsigned __stdcall job_thread(void *arg) {
  job_run((job_t *)arg);
  return 0;
}
static int run_on_thread(job_t *job) {
  uintptr_t h = _beginthreadex(NULL, 0, job_thread, job, 0, NULL);
  if (h == 0)
    return -1;
  WaitForSingleObject((HANDLE)h, INFINITE);
  CloseHandle((HANDLE)h);
  return 0;
}
#else
static void *job_thread(void *arg) {
  job_run((job_t *)arg);
  return NULL;
}
static int run_on_thread(job_t *job) {
  pthread_t t;
  if (pthread_create(&t, NULL, job_thread, job) != 0)
    return -1;
  return pthread_join(t, NULL);
}
#endif

static int bit(const void *buf, int64_t i) {
  return (((const uint8_t *)buf)[i >> 3] >> (i & 7)) & 1;
}

/* Back on R's thread: the batches into one R vector per column. Inside
   R_UnwindProtect, so an allocation error still releases every batch. */
static SEXP job_to_r(void *data) {
  job_t *job = (job_t *)data;
  const char *fields[] = {"code", "message", "names", "formats", "batches", "columns"};
  SEXP out = PROTECT(Rf_allocVector(VECSXP, 6));
  SEXP tags = PROTECT(Rf_allocVector(STRSXP, 6));
  for (int i = 0; i < 6; i++)
    SET_STRING_ELT(tags, i, Rf_mkChar(fields[i]));
  Rf_setAttrib(out, R_NamesSymbol, tags);
  SET_VECTOR_ELT(out, 0, Rf_ScalarInteger(job->code));
  SET_VECTOR_ELT(out, 1, Rf_mkString(job->message));
  SET_VECTOR_ELT(out, 4, Rf_ScalarInteger((int)job->nbatch));
  if (!job->schema.release) {
    UNPROTECT(2);
    return out;
  }

  int64_t ncol = job->schema.n_children, nrow = 0;
  for (int64_t b = 0; b < job->nbatch; b++)
    nrow += job->batches[b].length;
  SEXP names = PROTECT(Rf_allocVector(STRSXP, (R_xlen_t)ncol));
  SEXP formats = PROTECT(Rf_allocVector(STRSXP, (R_xlen_t)ncol));
  SEXP cols = PROTECT(Rf_allocVector(VECSXP, (R_xlen_t)ncol));
  for (int64_t j = 0; j < ncol; j++) {
    const struct ArrowSchema *cs = job->schema.children[j];
    SET_STRING_ELT(names, j, Rf_mkCharCE(cs->name, CE_UTF8));
    SET_STRING_ELT(formats, j, Rf_mkChar(cs->format));
    char f = cs->format[0];
    SEXPTYPE type = f == 'b' ? LGLSXP : f == 'i' ? INTSXP : f == 'g' ? REALSXP : STRSXP;
    SEXP col = Rf_allocVector(type, (R_xlen_t)nrow);
    SET_VECTOR_ELT(cols, j, col);
    R_xlen_t k = 0;
    for (int64_t b = 0; b < job->nbatch; b++) {
      const struct ArrowArray *a = job->batches[b].children[j];
      for (int64_t i = 0; i < a->length; i++, k++) {
        int64_t at = a->offset + i;
        int isnull = a->buffers[0] != NULL && !bit(a->buffers[0], at);
        if (f == 'b')
          LOGICAL(col)[k] = isnull ? NA_LOGICAL : bit(a->buffers[1], at);
        else if (f == 'i')
          INTEGER(col)[k] = isnull ? NA_INTEGER : ((const int32_t *)a->buffers[1])[at];
        else if (f == 'g')
          REAL(col)[k] = isnull ? NA_REAL : ((const double *)a->buffers[1])[at];
        else if (isnull)
          SET_STRING_ELT(col, k, NA_STRING);
        else {
          const int32_t *off = (const int32_t *)a->buffers[1];
          SET_STRING_ELT(col, k,
                         Rf_mkCharLenCE((const char *)a->buffers[2] + off[at],
                                        (int)(off[at + 1] - off[at]), CE_UTF8));
        }
      }
    }
  }
  Rf_setAttrib(cols, R_NamesSymbol, names);
  SET_VECTOR_ELT(out, 2, names);
  SET_VECTOR_ELT(out, 3, formats);
  SET_VECTOR_ELT(out, 5, cols);
  UNPROTECT(5);
  return out;
}

static void job_cleanup(void *data, Rboolean jump) {
  (void)jump;
  job_free((job_t *)data);
}

SEXP C_read_stream(SEXP path, SEXP header, SEXP delimiter, SEXP na, SEXP col_types,
                   SEXP batch_rows, SEXP threaded) {
  job_t job;
  memset(&job, 0, sizeof(job));
  job.path = Rf_translateChar(STRING_ELT(path, 0));
  zucsv_options_init(&job.opts);
  zucsv_options_set_strtod(&job.opts, probed_mode);
  int h = Rf_asLogical(header);
  job.opts.header = h == NA_LOGICAL ? ZUCSV_HEADER_DETECT : h ? ZUCSV_HEADER_YES : ZUCSV_HEADER_NO;
  job.opts.delimiter = CHAR(STRING_ELT(delimiter, 0))[0];
  job.opts.batch_rows = (int64_t)Rf_asReal(batch_rows);
  /* The strings are R's, alive for this call; zucsv copies them at open. */
  if (na == R_NilValue) {
    job.opts.na = NULL;
    job.opts.n_na = 0;
  } else {
    const char **s = (const char **)R_alloc((size_t)XLENGTH(na) + 1, sizeof(char *));
    for (R_xlen_t i = 0; i < XLENGTH(na); i++)
      s[i] = Rf_translateCharUTF8(STRING_ELT(na, i));
    job.opts.na = s;
    job.opts.n_na = XLENGTH(na);
  }
  if (col_types != R_NilValue) {
    const char **s = (const char **)R_alloc((size_t)XLENGTH(col_types) + 1, sizeof(char *));
    for (R_xlen_t i = 0; i < XLENGTH(col_types); i++)
      s[i] = CHAR(STRING_ELT(col_types, i));
    job.opts.col_types = s;
    job.opts.n_col_types = XLENGTH(col_types);
  }

  if (Rf_asLogical(threaded)) {
    if (run_on_thread(&job) != 0) {
      job_free(&job);
      Rf_error("could not start a thread");
    }
  } else {
    job_run(&job);
  }
  SEXP cont = PROTECT(R_MakeUnwindCont());
  SEXP out = R_UnwindProtect(job_to_r, &job, job_cleanup, &job, cont);
  job_free(&job);
  UNPROTECT(1);
  return out;
}

SEXP C_abi(void) {
  SEXP out = PROTECT(Rf_allocVector(INTSXP, 2));
  INTEGER(out)[0] = ZUCSV_ABI_VERSION;
  INTEGER(out)[1] = (int)zucsv_abi_version();
  UNPROTECT(1);
  return out;
}

SEXP C_strtod_mode(void) {
  return Rf_ScalarInteger(probed_mode);
}

static const R_CallMethodDef call_methods[] = {
    {"C_read_stream", (DL_FUNC)&C_read_stream, 7},
    {"C_abi", (DL_FUNC)&C_abi, 0},
    {"C_strtod_mode", (DL_FUNC)&C_strtod_mode, 0},
    {NULL, NULL, 0}};

attribute_visible void R_init_zucsvlink(DllInfo *dll) {
  R_registerRoutines(dll, NULL, call_methods, NULL, NULL);
  R_useDynamicSymbols(dll, FALSE);
  R_forceSymbols(dll, TRUE);
  /* Once, here, on R's main thread: the one call that needs R. */
  probed_mode = zucsv_strtod_probe();
}
