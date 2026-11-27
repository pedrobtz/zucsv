#ifndef ZUCSV_CORE_H
#define ZUCSV_CORE_H

/* The reader's R-free core (#23, design SS30): pass 1, which validates the
 * whole input and infers its types, and a pass-2 cursor over the data rows.
 * read_csv() and the Arrow stream are both its clients, so they cannot
 * disagree about a file. Nothing in core.c, convert.c or stream.c includes
 * an R header: they are what libzucsv.a holds. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "zsv.h"
#include "zucsv.h" /* inst/include: the public API */

/* ------------------------------------------------------------------ *
 * Limits (design SS15)
 * ------------------------------------------------------------------ */

/* Maximum columns zucsv will read. zsv truncates silently at its own
   max_columns, so the parser is configured with this value PLUS ONE and a
   record reporting more than the cap is an error: the spare slot is what
   distinguishes a file at the cap from one over it. */
#define ZUCSV_MAX_COLS 65536

/* Maximum bytes in one record. An oversize row reaches us as a zero-cell
   record (see tools/zsv-behavior.md), which is how it is detected.

   The real threshold is the parser's scan buffer, which it sizes at twice
   opts.max_row_size -- so that option is set to half of this, and this value
   is what the limit actually is. The buffer is malloc'd on every parse, so
   raising it costs memory on every call, including on tiny files. 8 MiB is
   128x zsv's own 64 KiB default, covers any realistic row, and leaves pages
   untouched (and so unpaged) unless rows really do get that large. */
#define ZUCSV_MAX_ROW_SIZE (8u * 1024u * 1024u)

/* Maximum data rows. Stricter than R_XLEN_T_MAX on purpose: a data frame's
   compact row names and nrow() are integers. */
#define ZUCSV_MAX_ROWS 2147483647

#if defined(__GNUC__) || defined(__clang__)
#define ZUCSV_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define ZUCSV_UNLIKELY(x) (x)
#endif

/* The progress hook runs every ZUCSV_INTERRUPT_INTERVAL records, or once
   ZUCSV_INTERRUPT_BYTES more input has been scanned, whichever comes first:
   with rows near the record limit, 16k records is gigabytes (#31). read_csv()
   checks for an interrupt there. */
#define ZUCSV_INTERRUPT_INTERVAL 16384
#define ZUCSV_INTERRUPT_BYTES ((size_t)64 * 1024 * 1024)

/* Bytes of a cell value shown in an error message before it is elided. */
#define ZUCSV_MSG_MAX 40

/* Default rows per stream batch. */
#define ZUCSV_BATCH_ROWS 65536

/* ------------------------------------------------------------------ *
 * Column types (design SS11)
 * ------------------------------------------------------------------ */

typedef enum {
  ZUCSV_LOGICAL = 0,
  ZUCSV_INTEGER = 1,
  ZUCSV_DOUBLE = 2,
  ZUCSV_CHARACTER = 3
} zucsv_type;

/* The na table: each entry is one UTF-8 string a cell may match exactly. */
typedef struct {
  const char **str;
  size_t *len;
  ptrdiff_t n;
} zucsv_na;

/* ------------------------------------------------------------------ *
 * convert.c
 * ------------------------------------------------------------------ */

/* TRUE when the cell matches any entry of the na table. */
int zucsv_is_na(const zucsv_na *na, const unsigned char *str, size_t len);

/* TRUE when the bytes are well-formed UTF-8. */
int zucsv_valid_utf8(const unsigned char *str, size_t len);

/* What a cell can carry into an R string. Checked in one pass over the
   bytes rather than two, since a separate NUL scan costs more than the
   UTF-8 walk itself. */
typedef enum {
  ZUCSV_TEXT_OK = 0,
  ZUCSV_TEXT_NUL = 1,
  ZUCSV_TEXT_BAD_UTF8 = 2
} zucsv_text_status;

zucsv_text_status zucsv_check_bytes(const unsigned char *str, size_t len);

/* --- grammars (design SS11) ---------------------------------------- *
 *
 * Each returns non-zero when the cell is syntactically that type. They are
 * deliberately stricter than the corresponding C library parser, so that
 * inference cannot be widened by syntax R would not accept either. The
 * checks never copy or modify the cell. */

/* Exactly TRUE, FALSE, true or false. */
int zucsv_is_logical(const unsigned char *str, size_t len);

/* [+-]?[0-9]+ that fits R's integer range. *out is set when it does. */
int zucsv_is_integer(const unsigned char *str, size_t len, int *out);

/* Decimal or scientific notation, plus Inf, -Inf, +Inf and NaN. */
int zucsv_is_double(const unsigned char *str, size_t len);

/* Converts a cell already known to satisfy zucsv_is_logical/is_double.
   The double conversion is a transcription of R's own R_strtod5 decimal
   path working on (ptr, len), in the instance `mode` names (a
   ZUCSV_STRTOD_* other than CALL); see convert.c. */
int zucsv_as_logical(const unsigned char *str, size_t len);
double zucsv_as_double(int mode, const unsigned char *str, size_t len);

/* --- per-column inference state ------------------------------------ */

typedef struct {
  int can_logical;
  int can_integer;
  int can_double;
  int all_missing;
} zucsv_infer;

void zucsv_infer_init(zucsv_infer *st);
zucsv_type zucsv_infer_result(const zucsv_infer *st);

/* Feeds one non-missing cell to a column's inference state.
   Returns non-zero when some grammar accepted the cell, which proves it is
   pure ASCII -- every grammar here accepts only ASCII -- so the caller can
   skip the NUL and UTF-8 checks entirely. A zero return means only that
   nothing was proved, never that the cell is bad. */
int zucsv_infer_update(zucsv_infer *st, const unsigned char *str, size_t len);

/* Whether a cell satisfies one specific type, for forced col_types. Same
   ASCII guarantee as above on a non-zero return. */
int zucsv_accepts(zucsv_type type, const unsigned char *str, size_t len);

const char *zucsv_type_name(zucsv_type t);

/* ------------------------------------------------------------------ *
 * core.c: the reader
 *
 * Every resource a read holds -- the FILE *, the parser, and each malloc'd
 * array -- hangs off one zucsv_core, and zucsv_core_close() releases all of
 * it from any state. So a client that cannot return normally from the
 * progress hook (read_csv(), whose hook may long-jump on an interrupt) only
 * has to close the core on its unwind path (design SS16).
 * ------------------------------------------------------------------ */

#define ZUCSV_MESSAGE_SIZE 8192

typedef struct zucsv_core {
  /* --- the source: a path, or borrowed bytes (data != NULL) --- */
  const char *path; /* NULL for text input */
  const char *data;
  size_t len;
  size_t pos;
  FILE *stream;
  zsv_parser parser;
  /* Set by the parser's diagnostic sink when zsv truncates a record that
     does not fit its buffer (design SS15), or the input ends inside a
     quoted cell (tools/patches/0003, #30). */
  int truncated;
  int unclosed;

  /* --- what to read --- */
  char delim;
  int want_header; /* ZUCSV_HEADER_* */
  zucsv_na na;
  /* NULL to infer; else n_forced (1 or ncol) types. Borrowed. */
  const zucsv_type *forced;
  ptrdiff_t n_forced;
  int strtod_mode;
  double (*strtod)(const char *, char **); /* ZUCSV_STRTOD_CALL only */
  /* Called where read_csv() checks for an interrupt; may be NULL. */
  void (*progress)(void *ctx);
  void *progress_ctx;

  /* --- what pass 1 found --- */
  ptrdiff_t ncol;
  double nrow;
  int header_row; /* the verdict, never "detect" */
  int have_names; /* names[] is filled */
  char **names;   /* ncol NUL-terminated UTF-8 names (or V1, V2, ...) */
  size_t *name_len;
  zucsv_type *types;
  zucsv_infer *infer;

  /* --- the pass-2 cursor --- */
  double record;
  double row; /* data rows returned so far */
  double at;  /* 0-based index of the current row */
  int header_pending;
  size_t next_check;

  /* --- scratch for ZUCSV_STRTOD_CALL --- */
  char *scratch;
  size_t scratch_cap;

  /* --- the first failure: an errno code and read_csv()'s message --- */
  int code;
  char message[ZUCSV_MESSAGE_SIZE];
} zucsv_core;

/* Zeroes the core and sets read_csv()'s defaults. */
void zucsv_core_init(zucsv_core *c);

/* Validates delimiter, header and the strtod mode: 0 or EINVAL. */
int zucsv_core_check_options(zucsv_core *c);

/* Pass 1. Returns 0, or the code also left in c->code with c->message. On
   return the source is closed. */
int zucsv_core_pass1(zucsv_core *c);

/* Opens the source again for pass 2. 0 or an error code. */
int zucsv_core_rewind(zucsv_core *c);

/* Advances to the next data row: 1 when the parser holds one (its index is
   c->at, its cells zsv_get_cell(c->parser, j)), 0 at the end, and -1 on
   failure (c->code). The end is where pass 2 is held to pass 1: a file
   that changed in between is an error, never another value (design SS9). */
int zucsv_core_next_row(zucsv_core *c);

/* A double cell's value in c's mode. With ZUCSV_STRTOD_CALL an allocation
   can fail: c->code is then ENOMEM and the value is 0. */
double zucsv_core_as_double(zucsv_core *c, const unsigned char *s, size_t n);

/* Releases everything; safe in any state and twice. */
void zucsv_core_close(zucsv_core *c);

/* Records the first failure and returns its code. */
int zucsv_core_fail(zucsv_core *c, int code, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

/* ------------------------------------------------------------------ *
 * stream.c
 * ------------------------------------------------------------------ */

/* zucsv_open_stream() over borrowed bytes, which must outlive the stream:
   for the tests and the fuzz harness. Not exported. */
int zucsv_open_stream_text(const char *data, size_t len, const zucsv_options *options,
                           struct ArrowArrayStream *out);

#endif /* ZUCSV_CORE_H */
