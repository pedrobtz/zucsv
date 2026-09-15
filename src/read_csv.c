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

/* The row loop stops on any status that is not zsv_status_row, and only one
   of those means the file ended: zsv_next_row() turns zsv_status_no_more_input
   into zsv_status_done before returning it (vendored zsv.c), so `done` is the
   only clean end a caller can see. A failed allocation inside the parser or an
   internal error looks identical to the loop, and neither sets ferror() on the
   stream, so without this a table would be silently truncated -- or a good file
   returned as 0x0 -- which is exactly what design SS16 forbids. */
static void zucsv_check_parse(enum zsv_status status, const char *path) {
  if (status == zsv_status_done)
    return;
  if (status == zsv_status_memory)
    Rf_error("Ran out of memory while parsing CSV file: %s", path);
  Rf_error("CSV parser failed while reading file: %s", path);
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
  /* zsv's default is to drop every leading record whose cells are all
     zero-length, using a blankness test that ignores quoting -- so a real
     first record of empty names, or a leading `""`, would never reach us and
     SS10's policy would be applied to the wrong record. zucsv decides what a
     blank record is (zucsv_is_blank_record(), decision 11); the parser must
     hand over all of them. */
  opts.keep_empty_header_rows = 1;

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

  switch (zucsv_check_bytes(c->str, c->len)) {
  case ZUCSV_TEXT_NUL:
    what = "an embedded NUL";
    break;
  case ZUCSV_TEXT_BAD_UTF8:
    what = "invalid UTF-8";
    break;
  default:
    return;
  }

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

/* `header = NA`: is the first record names or data? It is a header unless one
   of its *unquoted* fields is numeric or logical syntax -- one such field is
   enough, since a name that parses as a number is far rarer than a data row
   that does. Nothing outside this record is consulted: not the rest of the
   file, not `col_types`, not `na` (design SS4, SS29 decision 20). That is what
   lets the verdict be reached here, while the record is still the parser's
   current one, instead of at the end of pass 1. */
static int zucsv_detect_header(zsv_parser parser, R_xlen_t ncol) {
  for (R_xlen_t j = 0; j < ncol; j++) {
    struct zsv_cell c = zsv_get_cell(parser, (size_t)j);
    /* A quoted field is the writer saying "this is text", which is the same
       thing a column name says -- so it is skipped before either grammar:
       `"2024"` and `"TRUE"` are names where bare 2024 and TRUE are data
       (SS29 decision 21). */
    if (c.quoted & ZSV_PARSER_QUOTE_CLOSED)
      continue;
    /* Integer syntax is a subset of double syntax (SS29 decision 18), so the
       double grammar alone covers both. */
    if (zucsv_is_logical(c.str, c.len) || zucsv_is_double(c.str, c.len))
      return 0;
  }
  return 1;
}

typedef struct {
  zucsv_reader reader; /* first member: also the cleanup handle */
  const char *path;
  int want_header;
  char delim;
  /* Stop after pass 1 and return what it learned, instead of materialising
     columns. Sharing the pass is the point: a sniffer that ran its own
     inference could disagree with the reader it is meant to describe. */
  int sniff;
  zucsv_na na;
  SEXP col_types;
} zucsv_ctx;

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
  R_xlen_t ncol = 0;
  double nrow = 0;
  int nprotect = 0;
  zucsv_type *types = NULL;
  zucsv_infer *infer = NULL;
  /* ctx->want_header is what the caller asked for and may be NA (detect);
     header_row is the verdict, settled once below when the first record is
     read and never NA. Both passes read it, so pass 2 needs no re-check. */
  int header_row = 0;

  /* Every PROTECT below bumps nprotect and each return unwinds exactly that
     many, so the count never has to be kept in step by hand. The body of an
     R_UnwindProtect must leave the stack balanced. */

  /* ================= pass 1: inspect and validate ================= */
  SEXP names = R_NilValue;
  zucsv_open(&ctx->reader, ctx->path, ctx->delim);

  {
    double record = 0;
    int have_shape = 0;
    enum zsv_status status;

    while ((status = zsv_next_row(ctx->reader.parser)) == zsv_status_row) {
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

        header_row = (ctx->want_header == NA_LOGICAL)
                       ? zucsv_detect_header(ctx->reader.parser, ncol)
                       : ctx->want_header;

        if (header_row) {
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
        /* This record is data. It set ncol just above, and was shape-checked
           there, so the width check below would be trivially true for it. */
      } else {
        zucsv_check_shape(n, record);
        if ((R_xlen_t)n != ncol)
          Rf_error("CSV row %.0f has %d %s; expected %d", record, (int)n,
                   n == 1 ? "field" : "fields", (int)ncol);
      }

      for (R_xlen_t j = 0; j < ncol; j++) {
        struct zsv_cell c = zsv_get_cell(ctx->reader.parser, (size_t)j);

        /* A missing cell is evidence for nothing and is exempt from a
           forced type: it becomes that type's NA (design SS11, SS12). It
           also needs no byte check, being identical to an `na` string,
           which R already gave us as valid text. */
        if (zucsv_is_na(&ctx->na, c.str, c.len))
          continue;

        /* Every grammar accepts only ASCII, so a cell one of them takes
           cannot hold a NUL or a bad UTF-8 sequence and needs no walk over
           its bytes. Numeric and logical columns therefore pay nothing for
           the text checks; character columns pay exactly what they did
           before (design SS13). */
        if (ctx->col_types == R_NilValue) {
          if (!zucsv_infer_update(&infer[j], c.str, c.len))
            zucsv_check_text(&c, record, j, names);
        } else if (!zucsv_accepts(types[j], c.str, c.len)) {
          /* Report a NUL or bad UTF-8 in preference to a conversion
             failure: it says more about what is wrong with the file. */
          zucsv_check_text(&c, record, j, names);
          zucsv_check_forced(types[j], &c, record, j, names);
        }
      }

      if (nrow >= (double)ZUCSV_MAX_ROWS)
        Rf_error("CSV has more than %d rows, which is more than a data frame can hold",
                 ZUCSV_MAX_ROWS);
      nrow += 1;

      if ((R_xlen_t)record % ZUCSV_INTERRUPT_INTERVAL == 0)
        R_CheckUserInterrupt();
    }

    zucsv_check_parse(status, ctx->path);
  }

  zucsv_check_io(&ctx->reader, ctx->path);
  zucsv_reader_close(&ctx->reader);

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

  /* Inference is complete: fix each column's type before allocating. */
  if (ctx->col_types == R_NilValue) {
    for (R_xlen_t j = 0; j < ncol; j++)
      types[j] = zucsv_infer_result(&infer[j]);
  }

  if (ctx->sniff) {
    SEXP out = PROTECT(zucsv_sniff_result(header_row, ncol, nrow, names, types));
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

  zucsv_open(&ctx->reader, ctx->path, ctx->delim);

  {
    double record = 0, row = 0;
    int header_pending = header_row;
    enum zsv_status status;

    while ((status = zsv_next_row(ctx->reader.parser)) == zsv_status_row) {
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
        R_xlen_t at = (R_xlen_t)row;
        int missing = zucsv_is_na(&ctx->na, c.str, c.len);

        /* Pass 1 checked every cell against this column's type, so nothing
           here can fail on content (design SS9). */
        switch (types[j]) {
        case ZUCSV_LOGICAL:
          ((int *)colptr[j])[at] = missing ? NA_LOGICAL : zucsv_as_logical(c.str, c.len);
          break;
        case ZUCSV_INTEGER: {
          int value = NA_INTEGER;
          if (!missing)
            zucsv_is_integer(c.str, c.len, &value);
          ((int *)colptr[j])[at] = value;
          break;
        }
        case ZUCSV_DOUBLE:
          ((double *)colptr[j])[at] = missing ? NA_REAL : zucsv_as_double(c.str, c.len);
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
          if (s != NULL && (size_t)LENGTH(s) == c.len &&
              memcmp(CHAR(s), c.str, c.len) == 0) {
            SET_STRING_ELT(col, at, s);
          } else {
            s = Rf_mkCharLenCE((const char *)c.str, (int)c.len, CE_UTF8);
            SET_STRING_ELT(col, at, s);
            prev[j] = s;
          }
          break;
        }
        }
      }
      row += 1;

      if ((R_xlen_t)record % ZUCSV_INTERRUPT_INTERVAL == 0)
        R_CheckUserInterrupt();
    }

    zucsv_check_parse(status, ctx->path);
    /* Before concluding the file changed: a read error partway through ends
       the loop with a short table too, and "cannot read" is the truer
       message. Pass 1 checks in this order for the same reason. */
    zucsv_check_io(&ctx->reader, ctx->path);

    if (row != nrow)
      Rf_error("CSV file changed while it was being read: %s", ctx->path);
  }

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

/* The shared entry: both exported routines validate the same way and run the
   same body, so sniff_csv() cannot describe a file differently from how
   read_csv() would read it. `want_header` is already a resolved tri-state. */
static SEXP zucsv_run(SEXP file, int want_header, SEXP delimiter, SEXP na, SEXP col_types,
                      int sniff) {
  /* Arguments are re-validated here: the R wrapper's checks exist for the
     message, not for native safety (design SS7). */
  if (TYPEOF(file) != STRSXP || XLENGTH(file) != 1 || STRING_ELT(file, 0) == NA_STRING)
    Rf_error("'file' must be a single non-missing file path");
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
  ctx.want_header = want_header;
  ctx.delim = delim;
  ctx.sniff = sniff;
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

SEXP C_read_csv(SEXP file, SEXP header, SEXP delimiter, SEXP na, SEXP col_types) {
  if (TYPEOF(header) != LGLSXP || XLENGTH(header) != 1)
    Rf_error("'header' must be TRUE, FALSE or NA");
  return zucsv_run(file, LOGICAL(header)[0], delimiter, na, col_types, 0);
}

/* Always applies the first-record rule: reporting what `header = NA` would
   decide is the whole point of the function. */
SEXP C_sniff_csv(SEXP file, SEXP delimiter, SEXP na) {
  return zucsv_run(file, NA_LOGICAL, delimiter, na, R_NilValue, 1);
}
