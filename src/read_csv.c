/* read_csv(): the R-facing entry point (design SS4-SS16).
 *
 * Two passes over the file. Pass 1 validates everything and counts rows;
 * pass 2 allocates each column at its exact final length and fills it, so
 * it cannot fail on the content of the file (design SS9).
 *
 * The whole parse runs inside R_UnwindProtect, so the FILE * and the parser
 * are released however the call ends -- ordinary return, Rf_error(), or a
 * user interrupt. Every other allocation here is R_alloc'd and freed by R
 * on the same paths (design SS16). */

#include "zucsv.h"

#include <string.h>

/* ------------------------------------------------------------------ *
 * Parser plumbing
 * ------------------------------------------------------------------ */

/* Installed as opts.errprintf so no vendored code can write to the R
   session's stderr, whatever the input. */
static int zucsv_discard_diagnostics(void *ctx, const char *fmt, ...) {
  (void)ctx;
  (void)fmt;
  return 0;
}

/* A short read must not be mistaken for end of input. fopen() succeeds on a
   directory on most Unixes, and any I/O error mid-file would otherwise
   silently truncate the table. */
static void zucsv_check_io(zucsv_reader *r, const char *path) {
  if (r->stream && ferror(r->stream))
    Rf_error("Cannot read CSV file: %s", path);
}

static void zucsv_reader_close(zucsv_reader *r) {
  if (r->parser) {
    zsv_finish(r->parser);
    zsv_delete(r->parser);
    r->parser = NULL;
  }
  if (r->stream) {
    fclose(r->stream);
    r->stream = NULL;
  }
}

static void zucsv_cleanup(void *data, Rboolean jump) {
  (void)jump;
  zucsv_reader_close((zucsv_reader *)data);
}

static void zucsv_open(zucsv_reader *r, const char *path, char delimiter) {
  zucsv_reader_close(r);

  r->stream = fopen(path, "rb");
  if (!r->stream)
    Rf_error("Cannot open CSV file: %s", path);

  struct zsv_opts opts;
  memset(&opts, 0, sizeof(opts));
  opts.stream = r->stream;
  opts.errprintf = zucsv_discard_diagnostics;
  opts.delimiter = delimiter;
  /* One more than the cap: zsv truncates silently at max_columns, so the
     spare slot is what lets a file over the cap be told from one at it
     (design SS15, decision 12). */
  opts.max_columns = ZUCSV_MAX_COLS + 1;
  /* zsv sizes its scan buffer at 2 * max_row_size, and that buffer is the
     real limit on a record, so halve ours to land on ZUCSV_MAX_ROW_SIZE. */
  opts.max_row_size = ZUCSV_MAX_ROW_SIZE / 2;

  r->parser = zsv_new(&opts);
  if (!r->parser)
    Rf_error("Cannot create CSV parser");
}

/* ------------------------------------------------------------------ *
 * Records
 * ------------------------------------------------------------------ */

/* A blank record is one unquoted zero-length cell: a line with nothing
   between its terminators. `""` alone on a line is quoted, so it is a real
   one-cell record and is kept (design SS10, decision 11). */
static int zucsv_is_blank_record(zsv_parser parser, size_t ncell) {
  if (ncell != 1)
    return 0;
  struct zsv_cell c = zsv_get_cell(parser, 0);
  return c.len == 0 && !(c.quoted & ZSV_PARSER_QUOTE_CLOSED);
}

/* Column label for an error message: the header name, or V<n>. */
static const char *zucsv_col_label(SEXP names, R_xlen_t j, char *buf, size_t bufsize) {
  if (names != R_NilValue && j < XLENGTH(names))
    return Rf_translateChar(STRING_ELT(names, j));
  snprintf(buf, bufsize, "V%lld", (long long)(j + 1));
  return buf;
}

/* Rejects the two things that cannot be carried into an R string. Used for
   header names and for data cells alike (design SS13). */
static void zucsv_check_text(const struct zsv_cell *c, double record, R_xlen_t j, SEXP names) {
  char buf[64];
  const char *what = NULL;

  if (zucsv_find_nul(c->str, c->len) >= 0)
    what = "an embedded NUL";
  else if (!zucsv_valid_utf8(c->str, c->len))
    what = "invalid UTF-8";
  if (!what)
    return;

  /* A header record has no column names yet, so there is nothing to name. */
  if (names == R_NilValue)
    Rf_error("CSV contains %s at row %.0f, column %d", what, record, (int)(j + 1));
  Rf_error("CSV contains %s at row %.0f, column %d (\"%s\")", what, record, (int)(j + 1),
           zucsv_col_label(names, j, buf, sizeof(buf)));
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

static const char *zucsv_type_name(zucsv_type t) {
  switch (t) {
  case ZUCSV_LOGICAL:
    return "logical";
  case ZUCSV_INTEGER:
    return "integer";
  case ZUCSV_DOUBLE:
    return "double";
  default:
    return "character";
  }
}

/* Formats a cell for an error message: truncated, with anything that would
   break up the message flattened to a space. */
static void zucsv_show_cell(char *buf, size_t bufsize, const unsigned char *str, size_t len) {
  size_t n = len < ZUCSV_MSG_MAX ? len : ZUCSV_MSG_MAX;
  size_t j = 0;
  for (size_t i = 0; i < n && j + 4 < bufsize; i++) {
    unsigned char c = str[i];
    buf[j++] = (c == '\0' || c == '\n' || c == '\r' || c == '\t') ? ' ' : (char)c;
  }
  if (len > n && j + 4 < bufsize) {
    buf[j++] = '.';
    buf[j++] = '.';
    buf[j++] = '.';
  }
  buf[j] = '\0';
}

/* A cell that must satisfy a forced type but does not is an error, never a
   warning followed by NA (design SS12). */
static void zucsv_check_forced(zucsv_type type, const struct zsv_cell *c, double record,
                               R_xlen_t j, SEXP names) {
  int ok;
  switch (type) {
  case ZUCSV_LOGICAL:
    ok = zucsv_is_logical(c->str, c->len);
    break;
  case ZUCSV_INTEGER:
    ok = zucsv_is_integer(c->str, c->len, NULL);
    break;
  case ZUCSV_DOUBLE:
    ok = zucsv_is_double(c->str, c->len);
    break;
  default:
    return; /* character accepts any cell that got this far */
  }
  if (ok)
    return;

  char label[64], shown[ZUCSV_MSG_MAX + 8];
  zucsv_show_cell(shown, sizeof(shown), c->str, c->len);
  Rf_error("Cannot parse row %.0f, column %d (\"%s\") as %s: \"%s\"", record, (int)(j + 1),
           zucsv_col_label(names, j, label, sizeof(label)), zucsv_type_name(type), shown);
}

/* Shape errors that apply to any record, checked before its width is known
   to be right. `record` is 1-based as the parser emits records, header
   included (design SS10). */
static void zucsv_check_shape(size_t n, double record) {
  /* A zero-cell record is how an oversize row reaches us: the parser drops
     it silently and reports nothing (design SS15). */
  if (n == 0)
    Rf_error("CSV row %.0f exceeds zucsv's maximum record size of %u bytes", record,
             (unsigned)ZUCSV_MAX_ROW_SIZE);
  if (n > (size_t)ZUCSV_MAX_COLS)
    Rf_error("CSV exceeds zucsv's maximum of %d columns", ZUCSV_MAX_COLS);
}

/* ------------------------------------------------------------------ *
 * The parse
 * ------------------------------------------------------------------ */

typedef struct {
  zucsv_reader reader; /* first member: also the cleanup handle */
  const char *path;
  int want_header;
  char delim;
  zucsv_na na;
  SEXP col_types;
} zucsv_ctx;

static SEXP zucsv_read_body(void *data) {
  zucsv_ctx *ctx = (zucsv_ctx *)data;
  R_xlen_t ncol = 0;
  double nrow = 0;
  int nprotect = 0;
  zucsv_type *types = NULL;
  zucsv_infer *infer = NULL;

  /* Every PROTECT below bumps nprotect and each return unwinds exactly that
     many, so the count never has to be kept in step by hand. The body of an
     R_UnwindProtect must leave the stack balanced. */

  /* ================= pass 1: inspect and validate ================= */
  SEXP names = R_NilValue;
  zucsv_open(&ctx->reader, ctx->path, ctx->delim);

  {
    double record = 0;
    int have_shape = 0;

    while (zsv_next_row(ctx->reader.parser) == zsv_status_row) {
      record += 1;
      size_t n = zsv_cell_count(ctx->reader.parser);

      if (zucsv_is_blank_record(ctx->reader.parser, n))
        continue; /* blank records are skipped everywhere (design SS10) */

      if (!have_shape) {
        zucsv_check_shape(n, record);
        ncol = (R_xlen_t)n;
        have_shape = 1;

        names = PROTECT(Rf_allocVector(STRSXP, ncol));
        nprotect++;

        /* Per-column state, sized now that ncol is known. R_alloc'd, so it
           is released with everything else if this call unwinds. */
        types = (zucsv_type *)R_alloc((size_t)ncol, sizeof(zucsv_type));
        if (ctx->col_types == R_NilValue) {
          infer = (zucsv_infer *)R_alloc((size_t)ncol, sizeof(zucsv_infer));
          for (R_xlen_t j = 0; j < ncol; j++)
            zucsv_infer_init(&infer[j]);
        } else {
          R_xlen_t nt = XLENGTH(ctx->col_types);
          if (nt != 1 && nt != ncol)
            Rf_error("col_types has length %d but the CSV has %d columns", (int)nt, (int)ncol);
          for (R_xlen_t j = 0; j < ncol; j++)
            types[j] = zucsv_type_from_name(
              Rf_translateChar(STRING_ELT(ctx->col_types, nt == 1 ? 0 : j)));
        }

        if (ctx->want_header) {
          /* Header values are text: never na-matched, never type-inferred
             (design SS4), but still checked for NUL and UTF-8. */
          for (R_xlen_t j = 0; j < ncol; j++) {
            struct zsv_cell c = zsv_get_cell(ctx->reader.parser, (size_t)j);
            zucsv_check_text(&c, record, j, R_NilValue);
            SET_STRING_ELT(names, j, Rf_mkCharLenCE((const char *)c.str, (int)c.len, CE_UTF8));
          }
          continue; /* the header is not a data row */
        }

        char buf[32];
        for (R_xlen_t j = 0; j < ncol; j++) {
          snprintf(buf, sizeof(buf), "V%lld", (long long)(j + 1));
          SET_STRING_ELT(names, j, Rf_mkCharCE(buf, CE_UTF8));
        }
        /* with header = FALSE this record is data, so fall through */
      }

      zucsv_check_shape(n, record);
      if ((R_xlen_t)n != ncol)
        Rf_error("CSV row %.0f has %d %s; expected %d", record, (int)n,
                 n == 1 ? "field" : "fields", (int)ncol);

      for (R_xlen_t j = 0; j < ncol; j++) {
        struct zsv_cell c = zsv_get_cell(ctx->reader.parser, (size_t)j);
        zucsv_check_text(&c, record, j, names);

        /* A missing cell is evidence for nothing and is exempt from a
           forced type: it becomes that type's NA (design SS11, SS12). */
        if (zucsv_is_na(&ctx->na, c.str, c.len))
          continue;

        if (ctx->col_types == R_NilValue)
          zucsv_infer_update(&infer[j], c.str, c.len);
        else
          zucsv_check_forced(types[j], &c, record, j, names);
      }

      if (nrow >= (double)ZUCSV_MAX_ROWS)
        Rf_error("CSV has more than %d rows, which is more than a data frame can hold",
                 ZUCSV_MAX_ROWS);
      nrow += 1;

      if ((R_xlen_t)record % ZUCSV_INTERRUPT_INTERVAL == 0)
        R_CheckUserInterrupt();
    }
  }

  zucsv_check_io(&ctx->reader, ctx->path);
  zucsv_reader_close(&ctx->reader);

  /* An empty file, or one of nothing but blank records, is a zero-column,
     zero-row data frame (design SS14). */
  if (ncol == 0) {
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

  /* Inference is complete: fix each column's type before allocating. */
  if (ctx->col_types == R_NilValue) {
    for (R_xlen_t j = 0; j < ncol; j++)
      types[j] = zucsv_infer_result(&infer[j]);
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

  zucsv_open(&ctx->reader, ctx->path, ctx->delim);

  {
    double record = 0, row = 0;
    int header_pending = ctx->want_header;

    while (zsv_next_row(ctx->reader.parser) == zsv_status_row) {
      record += 1;
      size_t n = zsv_cell_count(ctx->reader.parser);

      if (zucsv_is_blank_record(ctx->reader.parser, n))
        continue;

      if (header_pending) {
        header_pending = 0;
        continue;
      }

      /* Pass 1 validated the content, so the only thing that can be wrong
         here is that the file changed underneath us. Checking is what keeps
         the writes below in bounds (design SS9). */
      if ((R_xlen_t)n != ncol || row >= nrow)
        Rf_error("CSV file changed while it was being read: %s", ctx->path);

      for (R_xlen_t j = 0; j < ncol; j++) {
        struct zsv_cell c = zsv_get_cell(ctx->reader.parser, (size_t)j);
        SEXP col = VECTOR_ELT(out, j);
        R_xlen_t at = (R_xlen_t)row;
        int missing = zucsv_is_na(&ctx->na, c.str, c.len);

        /* Pass 1 checked every cell against this column's type, so nothing
           here can fail on content (design SS9). */
        switch (types[j]) {
        case ZUCSV_LOGICAL:
          LOGICAL(col)[at] = missing ? NA_LOGICAL : zucsv_as_logical(c.str, c.len);
          break;
        case ZUCSV_INTEGER: {
          int value = NA_INTEGER;
          if (!missing)
            zucsv_is_integer(c.str, c.len, &value);
          INTEGER(col)[at] = value;
          break;
        }
        case ZUCSV_DOUBLE:
          REAL(col)[at] = missing ? NA_REAL : zucsv_as_double(c.str, c.len);
          break;
        default:
          SET_STRING_ELT(col, at,
                         missing ? NA_STRING
                                 : Rf_mkCharLenCE((const char *)c.str, (int)c.len, CE_UTF8));
          break;
        }
      }
      row += 1;

      if ((R_xlen_t)record % ZUCSV_INTERRUPT_INTERVAL == 0)
        R_CheckUserInterrupt();
    }

    if (row != nrow)
      Rf_error("CSV file changed while it was being read: %s", ctx->path);
  }

  zucsv_check_io(&ctx->reader, ctx->path);
  zucsv_reader_close(&ctx->reader);

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

SEXP C_read_csv(SEXP file, SEXP header, SEXP delimiter, SEXP na, SEXP col_types) {
  /* Arguments are re-validated here: the R wrapper's checks exist for the
     message, not for native safety (design SS7). */
  if (TYPEOF(file) != STRSXP || XLENGTH(file) != 1 || STRING_ELT(file, 0) == NA_STRING)
    Rf_error("'file' must be a single non-missing file path");
  if (TYPEOF(header) != LGLSXP || XLENGTH(header) != 1 || LOGICAL(header)[0] == NA_LOGICAL)
    Rf_error("'header' must be TRUE or FALSE");
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
  ctx.reader.stream = NULL;
  ctx.reader.parser = NULL;
  ctx.path = Rf_translateChar(STRING_ELT(file, 0));
  ctx.want_header = LOGICAL(header)[0];
  ctx.delim = delim;
  ctx.col_types = col_types;

  /* The na table, translated to UTF-8 once so matching is a bytewise
     comparison against the parser's cells (design SS4). */
  ctx.na.n = (na == R_NilValue) ? 0 : XLENGTH(na);
  ctx.na.str = ctx.na.n ? (const char **)R_alloc((size_t)ctx.na.n, sizeof(char *)) : NULL;
  ctx.na.len = ctx.na.n ? (size_t *)R_alloc((size_t)ctx.na.n, sizeof(size_t)) : NULL;
  for (R_xlen_t i = 0; i < ctx.na.n; i++) {
    SEXP e = STRING_ELT(na, i);
    if (e == NA_STRING)
      Rf_error("'na' may not contain NA");
    ctx.na.str[i] = Rf_translateCharUTF8(e);
    ctx.na.len[i] = strlen(ctx.na.str[i]);
  }

  SEXP cont = PROTECT(R_MakeUnwindCont());
  SEXP out = R_UnwindProtect(zucsv_read_body, &ctx, zucsv_cleanup, &ctx.reader, cont);
  UNPROTECT(1);
  return out;
}
