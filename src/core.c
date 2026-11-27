/* The reader without R (#23, design SS9-SS16, SS30).
 *
 * Two passes over the input. Pass 1 (zucsv_core_pass1) validates everything
 * and counts rows; pass 2 (zucsv_core_next_row) walks the data rows again,
 * so a client writes each cell into storage sized from pass 1 and cannot
 * fail on the content of the file (design SS9).
 *
 * Every check that raised an R error returns a code and leaves read_csv()'s
 * message in c->message, word for word: read_csv() raises it, and the
 * stream returns it from get_last_error(). The codes are errno values:
 * EINVAL for input or options read_csv() refuses, EIO for a file that
 * cannot be opened or read, ENOMEM. */

#include "core.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

int zucsv_core_fail(zucsv_core *c, int code, const char *fmt, ...) {
  if (c->code == 0) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->message, sizeof(c->message), fmt, ap);
    va_end(ap);
    c->code = code;
  }
  return c->code;
}

/* ------------------------------------------------------------------ *
 * Parser plumbing
 * ------------------------------------------------------------------ */

/* Installed as opts.errprintf so no vendored code can write to stderr,
   whatever the input. Nothing is printed. The one message that matters is
   kept as a flag: "Warning: row %zu truncated" is zsv's only report that a
   record overflowed its buffer. It then hands over the cells that fit as a
   record, and parses the rest of the line as further records, so without
   the flag an oversize record becomes a short row or vanishes (#28). Only
   zsv.c emits that message; the format is matched exactly. */
static int zucsv_diagnostics(void *ctx, const char *fmt, ...) {
  if (!ctx || !fmt)
    return 0;
  if (strcmp(fmt, "Warning: row %zu truncated\n") == 0)
    ((zucsv_core *)ctx)->truncated = 1;
  else if (strcmp(fmt, "Warning: unclosed quote at end of input\n") == 0)
    ((zucsv_core *)ctx)->unclosed = 1; /* tools/patches/0003, #30 */
  return 0;
}

/* A short read must not be mistaken for end of input. fopen() succeeds on a
   directory on most Unixes, and any I/O error mid-file would otherwise
   silently truncate the table. */
static int zucsv_check_io(zucsv_core *c) {
  if (c->stream && ferror(c->stream))
    return zucsv_core_fail(c, EIO, "Cannot read CSV file: %s", c->path);
  return 0;
}

/* The row loop stops on any status that is not zsv_status_row, and only one
   of those means the file ended: zsv_next_row() turns zsv_status_no_more_input
   into zsv_status_done before returning it (vendored zsv.c), so `done` is the
   only clean end a caller can see. A failed allocation inside the parser or an
   internal error looks identical to the loop, and neither sets ferror() on the
   stream, so without this a table would be silently truncated -- or a good file
   returned as 0x0 -- which is exactly what design SS16 forbids. */
static int zucsv_check_parse(zucsv_core *c, enum zsv_status status) {
  if (status == zsv_status_done)
    return 0;
  if (status == zsv_status_memory) {
    if (c->path)
      return zucsv_core_fail(c, ENOMEM, "Ran out of memory while parsing CSV file: %s", c->path);
    return zucsv_core_fail(c, ENOMEM, "Ran out of memory while parsing CSV text");
  }
  if (c->path)
    return zucsv_core_fail(c, EIO, "CSV parser failed while reading file: %s", c->path);
  return zucsv_core_fail(c, EIO, "CSV parser failed while reading text");
}

/* Pass 2 disagreeing with pass 1 means the file moved under us. Text cannot
   move -- the bytes are the caller's for the whole read -- so this is
   unreachable for text input and says so rather than naming a file that
   does not exist. */
static int zucsv_error_drift(zucsv_core *c) {
  if (c->path)
    return zucsv_core_fail(c, EIO, "CSV file changed while it was being read: %s", c->path);
  return zucsv_core_fail(c, EIO, "Internal error: CSV text parsed differently on the second pass");
}

static void zucsv_source_close(zucsv_core *c) {
  if (c->parser) {
    zsv_finish(c->parser);
    zsv_delete(c->parser);
    c->parser = NULL;
  }
  if (c->stream) {
    fclose(c->stream);
    c->stream = NULL;
  }
}

/* opts.read for text input. zsv calls it exactly as it would fread(), so the
   fread() convention is what it has to honour: n is the element size, size
   the count, and the return value is elements delivered, not bytes. */
static size_t zucsv_read_text(void *restrict buff, size_t n, size_t size, void *restrict ctx) {
  zucsv_core *c = (zucsv_core *)ctx;
  size_t want = n * size;
  size_t left = c->len - c->pos;
  size_t take = want < left ? want : left;
  if (take) {
    memcpy(buff, c->data + c->pos, take);
    c->pos += take;
  }
  return n ? take / n : 0;
}

/* One source per read: c->data set means text, otherwise path. Both passes
   call this, so for a file it is a fresh fopen and for text a rewind. */
static int zucsv_source_open(zucsv_core *c) {
  zucsv_source_close(c);

  if (c->data) {
    c->pos = 0;
  } else {
    c->stream = fopen(c->path, "rb");
    if (!c->stream)
      return zucsv_core_fail(c, EIO, "Cannot open CSV file: %s", c->path);
  }

  struct zsv_opts opts;
  memset(&opts, 0, sizeof(opts));
  if (c->data) {
    opts.read = zucsv_read_text;
    opts.stream = c;
  } else {
    opts.stream = c->stream;
  }
  opts.errprintf = zucsv_diagnostics;
  opts.errf = c;
  c->truncated = 0;
  c->unclosed = 0;
  opts.delimiter = c->delim;
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

  c->parser = zsv_new(&opts);
  if (!c->parser)
    return zucsv_core_fail(c, ENOMEM, "Cannot create CSV parser");
  return 0;
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
  struct zsv_cell cell = zsv_get_cell(parser, 0);
  return cell.len == 0 && !(cell.quoted & ZSV_PARSER_QUOTE_CLOSED);
}

/* Column label for an error message: the header name, or V<n>. `named` is
   whether names exist yet: the header record has none. */
static const char *zucsv_col_label(const zucsv_core *c, ptrdiff_t j, char *buf, size_t bufsize) {
  if (c->have_names && j < c->ncol)
    return c->names[j];
  snprintf(buf, bufsize, "V%lld", (long long)(j + 1));
  return buf;
}

/* Rejects the two things that cannot be carried into an R string, or an
   Arrow utf8 array. Used for header names and for data cells alike (design
   SS13). */
static int zucsv_check_text(zucsv_core *c, const struct zsv_cell *cell, double record,
                            ptrdiff_t j) {
  char buf[64];
  const char *what = NULL;

  switch (zucsv_check_bytes(cell->str, cell->len)) {
  case ZUCSV_TEXT_NUL:
    what = "an embedded NUL";
    break;
  case ZUCSV_TEXT_BAD_UTF8:
    what = "invalid UTF-8";
    break;
  default:
    return 0;
  }

  /* A header record has no column names yet, so there is nothing to name. */
  if (!c->have_names)
    return zucsv_core_fail(c, EINVAL, "CSV contains %s at row %.0f, column %d", what, record,
                           (int)(j + 1));
  return zucsv_core_fail(c, EINVAL, "CSV contains %s at row %.0f, column %d (\"%s\")", what,
                         record, (int)(j + 1), zucsv_col_label(c, j, buf, sizeof(buf)));
}

/* Formats a cell for an error message: truncated, with anything that would
   break up the message flattened to a space. */
static void zucsv_show_cell(char *buf, size_t bufsize, const unsigned char *str, size_t len) {
  size_t n = len < ZUCSV_MSG_MAX ? len : ZUCSV_MSG_MAX;
  size_t j = 0;
  for (size_t i = 0; i < n && j + 4 < bufsize; i++) {
    unsigned char ch = str[i];
    buf[j++] = (ch == '\0' || ch == '\n' || ch == '\r' || ch == '\t') ? ' ' : (char)ch;
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
static int zucsv_check_forced(zucsv_core *c, zucsv_type type, const struct zsv_cell *cell,
                              double record, ptrdiff_t j) {
  if (type == ZUCSV_CHARACTER || zucsv_accepts(type, cell->str, cell->len))
    return 0; /* character accepts any cell that got this far */

  char label[64], shown[ZUCSV_MSG_MAX + 8];
  zucsv_show_cell(shown, sizeof(shown), cell->str, cell->len);
  return zucsv_core_fail(c, EINVAL, "Cannot parse row %.0f, column %d (\"%s\") as %s: \"%s\"",
                         record, (int)(j + 1), zucsv_col_label(c, j, label, sizeof(label)),
                         zucsv_type_name(type), shown);
}

/* A record zsv truncated is an error at that record, checked before any of
   its cells is looked at: the cells it handed over are a prefix of the
   record, and the records after it are the rest of the same line (#28). */
static int zucsv_check_truncated(zucsv_core *c, double record) {
  if (c->truncated)
    return zucsv_core_fail(c, EINVAL, "CSV row %.0f exceeds zucsv's maximum record size of %u bytes",
                           record, (unsigned)(ZUCSV_MAX_ROW_SIZE - 1u));
  return 0;
}

/* The input ended inside a quoted cell. zsv reports it while returning the
   last record, whose last cell is the one the quote opened (#30). Checked
   before the record's width, so the error names the real cause. `named`
   is zero in pass 2, which reports no names. */
static int zucsv_check_unclosed(zucsv_core *c, double record, size_t n, int named) {
  if (!c->unclosed)
    return 0;
  /* Called with n = 0 only after the loop, as a backstop: the flag arrives
     with the record that holds the open cell, so this is not expected. */
  if (n == 0)
    return zucsv_core_fail(c, EINVAL, "CSV row %.0f opens a quote that is never closed", record);
  ptrdiff_t j = (ptrdiff_t)n - 1;
  /* A real name only: the header record has none yet, and a cell past the
     header's width has none, so neither gets an invented V<n> (as
     zucsv_check_text does). */
  if (!named || !c->have_names || j >= c->ncol)
    return zucsv_core_fail(c, EINVAL, "CSV row %.0f, column %d opens a quote that is never closed",
                           record, (int)(j + 1));
  return zucsv_core_fail(c, EINVAL,
                         "CSV row %.0f, column %d (\"%s\") opens a quote that is never closed",
                         record, (int)(j + 1), c->names[j]);
}

/* The progress hook, by records and by bytes (ZUCSV_INTERRUPT_BYTES, #31).
   zsv_cum_scanned_length() is a few loads, so calling it per record is
   cheap. Checked with the budget lowered to 4 MiB on a 19.6 MB file of
   64 KiB rows: it fired at 4.2, 8.4, 12.6 and 16.8 MB, where the record
   count alone (300 rows) never would. */
static void zucsv_maybe_progress(zucsv_core *c, double record, size_t *next_check) {
  if (!c->progress)
    return;
  size_t scanned = zsv_cum_scanned_length(c->parser);
  if ((ptrdiff_t)record % ZUCSV_INTERRUPT_INTERVAL == 0 || scanned >= *next_check) {
    *next_check = scanned + ZUCSV_INTERRUPT_BYTES;
    c->progress(c->progress_ctx);
  }
}

/* Shape errors that apply to any record, checked before its width is known
   to be right. `record` is 1-based as the parser emits records, header
   included (design SS10). */
static int zucsv_check_shape(zucsv_core *c, size_t n, double record) {
  /* A zero-cell record was zsv's only sign of an oversize row until #28;
     zucsv_check_truncated() now reports every oversize row first, so this
     stays as a backstop, not as the detector. */
  if (n == 0)
    return zucsv_core_fail(c, EINVAL, "CSV row %.0f exceeds zucsv's maximum record size of %u bytes",
                           record, (unsigned)(ZUCSV_MAX_ROW_SIZE - 1u));
  if (n > (size_t)ZUCSV_MAX_COLS)
    return zucsv_core_fail(c, EINVAL, "CSV exceeds zucsv's maximum of %d columns", ZUCSV_MAX_COLS);
  return 0;
}

/* `header = NA`: is the first record names or data? It is a header unless one
   of its *unquoted* fields is numeric or logical syntax -- one such field is
   enough, since a name that parses as a number is far rarer than a data row
   that does. Nothing outside this record is consulted: not the rest of the
   file, not `col_types`, not `na` (design SS4, SS29 decision 20). That is what
   lets the verdict be reached here, while the record is still the parser's
   current one, instead of at the end of pass 1. */
static int zucsv_detect_header(zsv_parser parser, ptrdiff_t ncol) {
  for (ptrdiff_t j = 0; j < ncol; j++) {
    struct zsv_cell cell = zsv_get_cell(parser, (size_t)j);
    /* A quoted field is the writer saying "this is text", which is the same
       thing a column name says -- so it is skipped before either grammar:
       `"2024"` and `"TRUE"` are names where bare 2024 and TRUE are data
       (SS29 decision 21). */
    if (cell.quoted & ZSV_PARSER_QUOTE_CLOSED)
      continue;
    /* Integer syntax is a subset of double syntax (SS29 decision 18), so the
       double grammar alone covers both. */
    if (zucsv_is_logical(cell.str, cell.len) || zucsv_is_double(cell.str, cell.len))
      return 0;
  }
  return 1;
}

/* ------------------------------------------------------------------ *
 * Set-up and tear-down
 * ------------------------------------------------------------------ */

void zucsv_core_init(zucsv_core *c) {
  memset(c, 0, sizeof(*c));
  c->delim = ',';
  c->want_header = ZUCSV_HEADER_YES;
  c->strtod_mode = ZUCSV_STRTOD_UNSET;
}

int zucsv_core_check_options(zucsv_core *c) {
  unsigned char d = (unsigned char)c->delim;
  if (d == 0 || d >= 0x80)
    return zucsv_core_fail(c, EINVAL, "'delimiter' must be a single ASCII byte");
  if (d == '\n' || d == '\r' || d == '\f' || d == '"')
    return zucsv_core_fail(c, EINVAL,
                           "'delimiter' may not be a newline, carriage return, form feed or quote");
  if (c->want_header != ZUCSV_HEADER_NO && c->want_header != ZUCSV_HEADER_YES &&
      c->want_header != ZUCSV_HEADER_DETECT)
    return zucsv_core_fail(c, EINVAL, "'header' must be ZUCSV_HEADER_YES, _NO or _DETECT");
  if (c->strtod_mode == ZUCSV_STRTOD_CALL) {
    if (!c->strtod)
      return zucsv_core_fail(c, EINVAL, "strtod_mode ZUCSV_STRTOD_CALL needs a strtod function");
  } else if (c->strtod_mode < ZUCSV_STRTOD_DOUBLE || c->strtod_mode > ZUCSV_STRTOD_DOUBLE_VERBATIM) {
    return zucsv_core_fail(c, EINVAL,
                           "strtod_mode must be set, from zucsv_strtod_probe() (zucsv-r.h)");
  } else if (!zucsv_strtod_usable(c->strtod_mode)) {
    return zucsv_core_fail(c, EINVAL, "strtod_mode %d is not usable on this machine",
                           c->strtod_mode);
  }
  return 0;
}

void zucsv_core_close(zucsv_core *c) {
  zucsv_source_close(c);
  if (c->names) {
    for (ptrdiff_t j = 0; j < c->ncol; j++)
      free(c->names[j]);
    free(c->names);
    c->names = NULL;
  }
  free(c->name_len);
  c->name_len = NULL;
  free(c->types);
  c->types = NULL;
  free(c->infer);
  c->infer = NULL;
  free(c->scratch);
  c->scratch = NULL;
  c->scratch_cap = 0;
  c->have_names = 0;
}

/* names[j] as a malloc'd NUL-terminated copy. */
static int zucsv_set_name(zucsv_core *c, ptrdiff_t j, const char *s, size_t len) {
  char *copy = (char *)malloc(len + 1);
  if (!copy)
    return zucsv_core_fail(c, ENOMEM, "Ran out of memory reading the CSV header");
  if (len)
    memcpy(copy, s, len);
  copy[len] = '\0';
  c->names[j] = copy;
  c->name_len[j] = len;
  return 0;
}

/* Per-column state, sized once ncol is known. Everything hangs off c, so
   zucsv_core_close() frees it on any path. */
static int zucsv_alloc_columns(zucsv_core *c, ptrdiff_t ncol) {
  c->ncol = ncol;
  c->names = (char **)calloc((size_t)ncol, sizeof(char *));
  c->name_len = (size_t *)calloc((size_t)ncol, sizeof(size_t));
  c->types = (zucsv_type *)calloc((size_t)ncol, sizeof(zucsv_type));
  if (!c->forced)
    c->infer = (zucsv_infer *)calloc((size_t)ncol, sizeof(zucsv_infer));
  if (!c->names || !c->name_len || !c->types || (!c->forced && !c->infer))
    return zucsv_core_fail(c, ENOMEM, "Ran out of memory reading a CSV of %d columns", (int)ncol);
  return 0;
}

/* ------------------------------------------------------------------ *
 * Pass 1: inspect and validate
 * ------------------------------------------------------------------ */

#define ZUCSV_TRY(x)                                                                               \
  do {                                                                                             \
    if ((x) != 0)                                                                                  \
      goto fail;                                                                                   \
  } while (0)

int zucsv_core_pass1(zucsv_core *c) {
  double record = 0;
  size_t next_check = ZUCSV_INTERRUPT_BYTES;
  int have_shape = 0;
  enum zsv_status status;

  c->ncol = 0;
  c->nrow = 0;
  c->header_row = 0;

  ZUCSV_TRY(zucsv_core_check_options(c));
  ZUCSV_TRY(zucsv_source_open(c));

  while ((status = zsv_next_row(c->parser)) == zsv_status_row) {
    record += 1;
    ZUCSV_TRY(zucsv_check_truncated(c, record));
    size_t n = zsv_cell_count(c->parser);
    ZUCSV_TRY(zucsv_check_unclosed(c, record, n, 1));
    /* At the top, so blank records and the header count too. */
    zucsv_maybe_progress(c, record, &next_check);

    if (zucsv_is_blank_record(c->parser, n))
      continue; /* blank records are skipped everywhere (design SS10) */

    if (!have_shape) {
      ZUCSV_TRY(zucsv_check_shape(c, n, record));
      have_shape = 1;
      ZUCSV_TRY(zucsv_alloc_columns(c, (ptrdiff_t)n));
      ptrdiff_t ncol = c->ncol;

      if (!c->forced) {
        for (ptrdiff_t j = 0; j < ncol; j++)
          zucsv_infer_init(&c->infer[j]);
      } else {
        if (c->n_forced != 1 && c->n_forced != ncol)
          ZUCSV_TRY(zucsv_core_fail(c, EINVAL, "col_types has length %d but the CSV has %d columns",
                                    (int)c->n_forced, (int)ncol));
        for (ptrdiff_t j = 0; j < ncol; j++)
          c->types[j] = c->forced[c->n_forced == 1 ? 0 : j];
      }

      c->header_row = (c->want_header == ZUCSV_HEADER_DETECT)
                        ? zucsv_detect_header(c->parser, ncol)
                        : c->want_header == ZUCSV_HEADER_YES;

      if (c->header_row) {
        /* Header values are text: never na-matched, never type-inferred
           (design SS4), but still checked for NUL and UTF-8. */
        for (ptrdiff_t j = 0; j < ncol; j++) {
          struct zsv_cell cell = zsv_get_cell(c->parser, (size_t)j);
          ZUCSV_TRY(zucsv_check_text(c, &cell, record, j));
          ZUCSV_TRY(zucsv_set_name(c, j, (const char *)cell.str, cell.len));
        }
        c->have_names = 1;
        continue; /* the header is not a data row */
      }

      char buf[32];
      for (ptrdiff_t j = 0; j < ncol; j++) {
        int len = snprintf(buf, sizeof(buf), "V%lld", (long long)(j + 1));
        ZUCSV_TRY(zucsv_set_name(c, j, buf, (size_t)len));
      }
      c->have_names = 1;
      /* This record is data. It set ncol just above, and was shape-checked
         there, so the width check below would be trivially true for it. */
    } else {
      ZUCSV_TRY(zucsv_check_shape(c, n, record));
      if ((ptrdiff_t)n != c->ncol)
        ZUCSV_TRY(zucsv_core_fail(c, EINVAL, "CSV row %.0f has %d %s; expected %d", record, (int)n,
                                  n == 1 ? "field" : "fields", (int)c->ncol));
    }

    for (ptrdiff_t j = 0; j < c->ncol; j++) {
      struct zsv_cell cell = zsv_get_cell(c->parser, (size_t)j);

      /* A missing cell is evidence for nothing and is exempt from a forced
         type: it becomes that type's NA (design SS11, SS12). It also needs
         no byte check, being identical to an `na` string. */
      if (zucsv_is_na(&c->na, cell.str, cell.len))
        continue;

      /* Every grammar accepts only ASCII, so a cell one of them takes
         cannot hold a NUL or a bad UTF-8 sequence and needs no walk over
         its bytes. Numeric and logical columns therefore pay nothing for
         the text checks; character columns pay exactly what they did
         before (design SS13). */
      if (!c->forced) {
        if (!zucsv_infer_update(&c->infer[j], cell.str, cell.len))
          ZUCSV_TRY(zucsv_check_text(c, &cell, record, j));
      } else if (!zucsv_accepts(c->types[j], cell.str, cell.len)) {
        /* Report a NUL or bad UTF-8 in preference to a conversion failure:
           it says more about what is wrong with the file. */
        ZUCSV_TRY(zucsv_check_text(c, &cell, record, j));
        ZUCSV_TRY(zucsv_check_forced(c, c->types[j], &cell, record, j));
      }
    }

    if (c->nrow >= (double)ZUCSV_MAX_ROWS)
      ZUCSV_TRY(zucsv_core_fail(c, EINVAL,
                                "CSV has more than %d rows, which is more than a data frame can hold",
                                ZUCSV_MAX_ROWS));
    c->nrow += 1;
  }

  /* A truncation reported with no record after it: the last record of the
     input overflowed. */
  ZUCSV_TRY(zucsv_check_truncated(c, record + 1));
  ZUCSV_TRY(zucsv_check_unclosed(c, record, 0, 0));
  ZUCSV_TRY(zucsv_check_parse(c, status));
  ZUCSV_TRY(zucsv_check_io(c));
  zucsv_source_close(c);

  /* Inference is complete: fix each column's type. */
  if (!c->forced) {
    for (ptrdiff_t j = 0; j < c->ncol; j++)
      c->types[j] = zucsv_infer_result(&c->infer[j]);
  }
  return 0;

fail:
  zucsv_source_close(c);
  return c->code;
}

/* ------------------------------------------------------------------ *
 * Pass 2: the data rows again
 * ------------------------------------------------------------------ */

int zucsv_core_rewind(zucsv_core *c) {
  if (c->code)
    return c->code;
  c->record = 0;
  c->row = 0;
  c->at = 0;
  c->header_pending = c->header_row;
  c->next_check = ZUCSV_INTERRUPT_BYTES;
  return zucsv_source_open(c);
}

int zucsv_core_next_row(zucsv_core *c) {
  if (c->code)
    return -1;
  if (!c->parser)
    return 0; /* already at the end */

  enum zsv_status status;
  while ((status = zsv_next_row(c->parser)) == zsv_status_row) {
    c->record += 1;
    if (zucsv_check_truncated(c, c->record))
      return -1;
    size_t n = zsv_cell_count(c->parser);
    if (zucsv_check_unclosed(c, c->record, n, 0))
      return -1;
    /* At the top, so blank records and the header count too. */
    zucsv_maybe_progress(c, c->record, &c->next_check);

    if (zucsv_is_blank_record(c->parser, n))
      continue;

    if (c->header_pending) {
      c->header_pending = 0;
      continue;
    }

    /* Pass 1 validated the content, so the only thing that can be wrong
       here is that the file changed underneath us. Checking is what keeps
       a client's writes in bounds (design SS9). */
    if ((ptrdiff_t)n != c->ncol || c->row >= c->nrow) {
      zucsv_error_drift(c);
      return -1;
    }
    c->at = c->row;
    c->row += 1;
    return 1;
  }

  /* A truncation reported with no record after it: the last record of the
     input overflowed. */
  if (zucsv_check_truncated(c, c->record + 1) || zucsv_check_unclosed(c, c->record, 0, 0) ||
      zucsv_check_parse(c, status) ||
      /* Before concluding the file changed: a read error partway through
         ends the loop with a short table too, and "cannot read" is the
         truer message. Pass 1 checks in this order for the same reason. */
      zucsv_check_io(c))
    return -1;
  if (c->row != c->nrow) {
    zucsv_error_drift(c);
    return -1;
  }
  zucsv_source_close(c);
  return 0;
}

/* The fallback, and what zucsv did before the transcription: R_strtod
   needs a terminator. The scratch buffer grows to the longest cell. */
static double zucsv_strtod_call(zucsv_core *c, const unsigned char *s, size_t n) {
  char stack_buf[256];
  char *buf = stack_buf;
  if (n + 1 > sizeof(stack_buf)) {
    if (n + 1 > c->scratch_cap) {
      char *grown = (char *)realloc(c->scratch, n + 1);
      /* A silent NaN would be a wrong value (design SS20, #36). */
      if (!grown) {
        zucsv_core_fail(c, ENOMEM, "Ran out of memory converting a %lu-byte numeric cell",
                        (unsigned long)n);
        return 0;
      }
      c->scratch = grown;
      c->scratch_cap = n + 1;
    }
    buf = c->scratch;
  }
  memcpy(buf, s, n);
  buf[n] = '\0';
  char *end;
  return c->strtod(buf, &end);
}

double zucsv_core_as_double(zucsv_core *c, const unsigned char *s, size_t n) {
  if (ZUCSV_UNLIKELY(c->strtod_mode == ZUCSV_STRTOD_CALL)) {
    /* Inf and NaN by name first, as in every mode: zucsv_as_double()
       answers them before it looks at the mode. */
    size_t i = (n > 0 && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
    if (n - i == 3 && (memcmp(s + i, "Inf", 3) == 0 || memcmp(s, "NaN", 3) == 0))
      return zucsv_as_double(ZUCSV_STRTOD_DOUBLE, s, n);
    return zucsv_strtod_call(c, s, n);
  }
  return zucsv_as_double(c->strtod_mode, s, n);
}
