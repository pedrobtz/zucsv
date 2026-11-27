/* read_csv() and sniff_csv(): the R-facing entry points (design SS4-SS16).
 *
 * Both are clients of the R-free core (core.c, #23): pass 1 validates
 * everything and counts rows there, and pass 2 walks the same rows with
 * the core's cursor while this file writes each cell into an R vector
 * allocated at its exact final length, so it cannot fail on the content of
 * the file (design SS9). The Arrow stream is the core's other client, so
 * the two cannot read a file differently.
 *
 * The whole read runs inside R_UnwindProtect, and every resource it holds
 * -- the FILE *, the parser, the core's arrays -- hangs off one zucsv_core,
 * so the cleanup releases it however the call ends: ordinary return,
 * Rf_error(), or a user interrupt raised from the core's progress hook
 * (design SS16). Scratch on this side is R_alloc'd and freed by R. */

#include "rzucsv.h"

#include <string.h>

/* The core's progress hook: read_csv() stays interruptible, every 16k
   records or 64 MiB (#31). R_CheckUserInterrupt() long-jumps out of the
   core's frames, which hold nothing the cleanup below does not release. */
static void zucsv_r_progress(void *ctx) {
  (void)ctx;
  R_CheckUserInterrupt();
}

/* The core's message as an R error. Column names in it are UTF-8, as the
   header held them, so it is translated to the native encoding as the
   names were before the core existed. */
static void zucsv_r_fail(const zucsv_core *c) {
  Rf_error("%s", Rf_translateChar(Rf_mkCharCE(c->message, CE_UTF8)));
}

typedef struct {
  zucsv_core core; /* first member: also the cleanup handle */
  /* Stop after pass 1 and return what it learned, instead of materialising
     columns. Sharing the pass is the point: a sniffer that ran its own
     inference could disagree with the reader it is meant to describe. */
  int sniff;
} zucsv_ctx;

static void zucsv_cleanup(void *data, Rboolean jump) {
  (void)jump;
  zucsv_core_close((zucsv_core *)data);
}

/* The sniff result: a plain list, so it composes straight into read_csv()
   and adds no S3 surface (design SS5). */
static SEXP zucsv_sniff_result(int header_row, R_xlen_t ncol, double nrow, SEXP names,
                               const zucsv_type *types) {
  const char *fields[] = {"header", "ncol", "nrow", "col_names", "col_types"};
  const int nfield = 5;

  SEXP out = PROTECT(Rf_allocVector(VECSXP, nfield));
  SEXP tags = PROTECT(Rf_allocVector(STRSXP, nfield));
  for (int i = 0; i < nfield; i++)
    SET_STRING_ELT(tags, i, Rf_mkChar(fields[i]));
  Rf_setAttrib(out, R_NamesSymbol, tags);

  SET_VECTOR_ELT(out, 0, Rf_ScalarLogical(header_row));
  SET_VECTOR_ELT(out, 1, Rf_ScalarInteger((int)ncol));
  /* nrow is bounded by ZUCSV_MAX_ROWS == INT_MAX (decision 7), so it fits. */
  SET_VECTOR_ELT(out, 2, Rf_ScalarInteger((int)nrow));
  SET_VECTOR_ELT(out, 3, names == R_NilValue ? Rf_allocVector(STRSXP, 0) : names);

  SEXP tnames = PROTECT(Rf_allocVector(STRSXP, ncol));
  for (R_xlen_t j = 0; j < ncol; j++)
    SET_STRING_ELT(tnames, j, Rf_mkChar(zucsv_type_name(types[j])));
  SET_VECTOR_ELT(out, 4, tnames);

  UNPROTECT(3);
  return out;
}

static SEXP zucsv_read_body(void *data) {
  zucsv_ctx *ctx = (zucsv_ctx *)data;
  zucsv_core *c = &ctx->core;
  int nprotect = 0;

  /* Every PROTECT below bumps nprotect and each return unwinds exactly that
     many, so the count never has to be kept in step by hand. The body of an
     R_UnwindProtect must leave the stack balanced. */

  /* ================= pass 1: inspect and validate ================= */
  if (zucsv_core_pass1(c))
    zucsv_r_fail(c);

  R_xlen_t ncol = (R_xlen_t)c->ncol;
  double nrow = c->nrow;

  /* An empty file, or one of nothing but blank records, is a zero-column,
     zero-row data frame (design SS14). */
  if (ncol == 0) {
    if (ctx->sniff) {
      SEXP out = PROTECT(zucsv_sniff_result(0, 0, 0, R_NilValue, NULL));
      nprotect++;
      UNPROTECT(nprotect);
      return out;
    }
    SEXP out = PROTECT(Rf_allocVector(VECSXP, 0));
    nprotect++;
    Rf_setAttrib(out, R_NamesSymbol, PROTECT(Rf_allocVector(STRSXP, 0)));
    nprotect++;
    Rf_setAttrib(out, R_RowNamesSymbol, PROTECT(Rf_allocVector(INTSXP, 0)));
    nprotect++;
    Rf_classgets(out, PROTECT(Rf_mkString("data.frame")));
    nprotect++;
    UNPROTECT(nprotect);
    return out;
  }

  SEXP names = PROTECT(Rf_allocVector(STRSXP, ncol));
  nprotect++;
  for (R_xlen_t j = 0; j < ncol; j++)
    SET_STRING_ELT(names, j, Rf_mkCharLenCE(c->names[j], (int)c->name_len[j], CE_UTF8));

  const zucsv_type *types = c->types;

  if (ctx->sniff) {
    SEXP out = PROTECT(zucsv_sniff_result(c->header_row, ncol, nrow, names, types));
    nprotect++;
    UNPROTECT(nprotect);
    return out;
  }

  /* ================= pass 2: materialize ================= */
  SEXP out = PROTECT(Rf_allocVector(VECSXP, ncol));
  nprotect++;
  for (R_xlen_t j = 0; j < ncol; j++) {
    SEXPTYPE sxp;
    switch (types[j]) {
    case ZUCSV_LOGICAL:
      sxp = LGLSXP;
      break;
    case ZUCSV_INTEGER:
      sxp = INTSXP;
      break;
    case ZUCSV_DOUBLE:
      sxp = REALSXP;
      break;
    default:
      sxp = STRSXP;
      break;
    }
    SET_VECTOR_ELT(out, j, Rf_allocVector(sxp, (R_xlen_t)nrow));
  }

  /* Data pointers taken once per column rather than per cell. VECTOR_ELT()
     and LOGICAL()/INTEGER()/REAL() are function calls in the R API and
     measured together at 4 ns per cell -- as much as the UTF-8 walk was.
     Holding the pointers across the mkCharLenCE() allocations below is safe
     because R's collector does not move vectors and every column stays
     reachable from `out`, which is protected. Character columns are left
     to VECTOR_ELT per cell: SET_STRING_ELT needs the SEXP, and their cost
     is the string creation anyway. */
  void **colptr = (void **)R_alloc((size_t)ncol, sizeof(void *));

  /* Per-column run-length reuse of the previous CHARSXP. Real categorical
     columns repeat heavily -- KEN_ALL's prefecture column is 47 distinct
     values over 124k rows -- and one memcmp is far cheaper than hashing
     into R's global string cache: 8.0 ns against 24.1 on that column, and
     about 7% slower when nothing repeats.

     The comparison is against the stored CHARSXP's own bytes, never against
     the previous cell's: those live in the parser's buffer, which is reused
     as parsing advances. CHAR(s) is stable R memory for as long as s lives,
     and each entry here is an element of a column of `out`, which is
     protected -- so these SEXPs stay reachable without protection of their
     own, which is why R_alloc'd storage is safe for them. */
  SEXP *prev = (SEXP *)R_alloc((size_t)ncol, sizeof(SEXP));
  for (R_xlen_t j = 0; j < ncol; j++)
    prev[j] = NULL;

  for (R_xlen_t j = 0; j < ncol; j++) {
    SEXP col = VECTOR_ELT(out, j);
    switch (types[j]) {
    case ZUCSV_LOGICAL:
      colptr[j] = LOGICAL(col);
      break;
    case ZUCSV_INTEGER:
      colptr[j] = INTEGER(col);
      break;
    case ZUCSV_DOUBLE:
      colptr[j] = REAL(col);
      break;
    default:
      colptr[j] = NULL;
      break;
    }
  }

  if (zucsv_core_rewind(c))
    zucsv_r_fail(c);

  int more;
  while ((more = zucsv_core_next_row(c)) == 1) {
    R_xlen_t at = (R_xlen_t)c->at;
    for (R_xlen_t j = 0; j < ncol; j++) {
      struct zsv_cell cell = zsv_get_cell(c->parser, (size_t)j);
      int missing = zucsv_is_na(&c->na, cell.str, cell.len);

      /* Pass 1 checked every cell against this column's type, so nothing
         here can fail on content (design SS9). */
      switch (types[j]) {
      case ZUCSV_LOGICAL:
        ((int *)colptr[j])[at] = missing ? NA_LOGICAL : zucsv_as_logical(cell.str, cell.len);
        break;
      case ZUCSV_INTEGER: {
        int value = NA_INTEGER;
        if (!missing)
          zucsv_is_integer(cell.str, cell.len, &value);
        ((int *)colptr[j])[at] = value;
        break;
      }
      case ZUCSV_DOUBLE:
        ((double *)colptr[j])[at] = missing ? NA_REAL : zucsv_core_as_double(c, cell.str, cell.len);
        break;
      default: {
        SEXP col = VECTOR_ELT(out, j);
        if (missing) {
          SET_STRING_ELT(col, at, NA_STRING);
          break; /* NA_STRING is never a reuse candidate: CHAR() of it is
                    "NA", which would match a literal NA cell under
                    na = NULL */
        }
        SEXP s = prev[j];
        if (s != NULL && (size_t)LENGTH(s) == cell.len &&
            memcmp(CHAR(s), cell.str, cell.len) == 0) {
          SET_STRING_ELT(col, at, s);
        } else {
          s = Rf_mkCharLenCE((const char *)cell.str, (int)cell.len, CE_UTF8);
          SET_STRING_ELT(col, at, s);
          prev[j] = s;
        }
        break;
      }
      }
    }
    /* Only the R_strtod fallback can fail here, on an allocation. */
    if (ZUCSV_UNLIKELY(c->code))
      zucsv_r_fail(c);
  }
  if (more < 0)
    zucsv_r_fail(c);

  zucsv_core_close(c);

  /* --- the data frame, built directly (design SS5) --- */
  Rf_setAttrib(out, R_NamesSymbol, names);

  SEXP rn;
  if (nrow == 0) {
    rn = PROTECT(Rf_allocVector(INTSXP, 0));
  } else {
    rn = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(rn)[0] = NA_INTEGER;
    INTEGER(rn)[1] = -(int)nrow;
  }
  nprotect++;
  Rf_setAttrib(out, R_RowNamesSymbol, rn);
  Rf_classgets(out, PROTECT(Rf_mkString("data.frame")));
  nprotect++;

  UNPROTECT(nprotect);
  return out;
}

/* Maps a col_types name to the enum. The R wrapper has already rejected
   anything not in this set. */
static zucsv_type zucsv_type_from_name(const char *name) {
  if (strcmp(name, "logical") == 0)
    return ZUCSV_LOGICAL;
  if (strcmp(name, "integer") == 0)
    return ZUCSV_INTEGER;
  if (strcmp(name, "double") == 0)
    return ZUCSV_DOUBLE;
  return ZUCSV_CHARACTER;
}

/* The shared entry: both exported routines validate the same way and run the
   same body, so sniff_csv() cannot describe a file differently from how
   read_csv() would read it. `want_header` is already a resolved tri-state. */
static SEXP zucsv_run(SEXP file, SEXP text, int want_header, SEXP delimiter, SEXP na,
                      SEXP col_types, int sniff) {
  /* Arguments are re-validated here: the R wrapper's checks exist for the
     message, not for native safety (design SS7). */
  if ((file == R_NilValue) == (text == R_NilValue))
    Rf_error("Supply exactly one of 'file' and 'text'");
  if (file != R_NilValue &&
      (TYPEOF(file) != STRSXP || XLENGTH(file) != 1 || STRING_ELT(file, 0) == NA_STRING))
    Rf_error("'file' must be a single non-missing file path");
  if (text != R_NilValue &&
      (TYPEOF(text) != STRSXP || XLENGTH(text) != 1 || STRING_ELT(text, 0) == NA_STRING))
    Rf_error("'text' must be a single non-missing string");
  if (TYPEOF(delimiter) != STRSXP || XLENGTH(delimiter) != 1 || STRING_ELT(delimiter, 0) == NA_STRING)
    Rf_error("'delimiter' must be a single character");
  if (na != R_NilValue && TYPEOF(na) != STRSXP)
    Rf_error("'na' must be a character vector or NULL");
  if (col_types != R_NilValue && TYPEOF(col_types) != STRSXP)
    Rf_error("'col_types' must be a character vector or NULL");

  const char *delim_str = CHAR(STRING_ELT(delimiter, 0));
  if (strlen(delim_str) != 1 || (unsigned char)delim_str[0] >= 0x80)
    Rf_error("'delimiter' must be a single ASCII byte");
  char delim = delim_str[0];
  if (delim == '\n' || delim == '\r' || delim == '\f' || delim == '"')
    Rf_error("'delimiter' may not be a newline, carriage return, form feed or quote");

  zucsv_ctx ctx;
  zucsv_core_init(&ctx.core);
  zucsv_core *c = &ctx.core;
  if (file != R_NilValue) {
    c->path = Rf_translateChar(STRING_ELT(file, 0));
  } else {
    /* Translated once, to UTF-8, so the parser sees the same bytes a file
       would have held -- and so a latin1- or native-encoded string is
       converted rather than rejected. The result lives on R's vmax stack
       until .Call returns, which outlives both passes. */
    c->data = Rf_translateCharUTF8(STRING_ELT(text, 0));
    c->len = strlen(c->data);
  }
  c->want_header = want_header == NA_LOGICAL ? ZUCSV_HEADER_DETECT
                   : want_header            ? ZUCSV_HEADER_YES
                                            : ZUCSV_HEADER_NO;
  c->delim = delim;
  c->strtod_mode = zucsv_r_strtod_mode;
  c->strtod = R_strtod; /* used only in ZUCSV_STRTOD_CALL */
  c->progress = zucsv_r_progress;
  ctx.sniff = sniff;

  if (col_types != R_NilValue) {
    R_xlen_t nt = XLENGTH(col_types);
    zucsv_type *forced = (zucsv_type *)R_alloc((size_t)(nt ? nt : 1), sizeof(zucsv_type));
    for (R_xlen_t i = 0; i < nt; i++)
      forced[i] = zucsv_type_from_name(Rf_translateChar(STRING_ELT(col_types, i)));
    c->forced = forced;
    c->n_forced = (ptrdiff_t)nt;
  }

  /* The na table, translated to UTF-8 once so matching is a bytewise
     comparison against the parser's cells (design SS4). */
  c->na.n = (na == R_NilValue) ? 0 : (ptrdiff_t)XLENGTH(na);
  c->na.str = c->na.n ? (const char **)R_alloc((size_t)c->na.n, sizeof(char *)) : NULL;
  c->na.len = c->na.n ? (size_t *)R_alloc((size_t)c->na.n, sizeof(size_t)) : NULL;
  for (ptrdiff_t i = 0; i < c->na.n; i++) {
    SEXP e = STRING_ELT(na, i);
    if (e == NA_STRING)
      Rf_error("'na' may not contain NA");
    c->na.str[i] = Rf_translateCharUTF8(e);
    c->na.len[i] = strlen(c->na.str[i]);
  }

  SEXP cont = PROTECT(R_MakeUnwindCont());
  SEXP out = R_UnwindProtect(zucsv_read_body, &ctx, zucsv_cleanup, &ctx.core, cont);
  UNPROTECT(1);
  return out;
}

SEXP C_read_csv(SEXP file, SEXP text, SEXP header, SEXP delimiter, SEXP na, SEXP col_types) {
  if (TYPEOF(header) != LGLSXP || XLENGTH(header) != 1)
    Rf_error("'header' must be TRUE, FALSE or NA");
  return zucsv_run(file, text, LOGICAL(header)[0], delimiter, na, col_types, 0);
}

/* Always applies the first-record rule: reporting what `header = NA` would
   decide is the whole point of the function. */
SEXP C_sniff_csv(SEXP file, SEXP text, SEXP delimiter, SEXP na) {
  return zucsv_run(file, text, NA_LOGICAL, delimiter, na, R_NilValue, 1);
}
