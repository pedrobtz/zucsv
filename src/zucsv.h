#ifndef ZUCSV_H
#define ZUCSV_H

#define R_NO_REMAP
#include <R.h>
#include <Rinternals.h>

#include "zsv.h"

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
#define ZUCSV_NOINLINE __attribute__((noinline))
#else
#define ZUCSV_UNLIKELY(x) (x)
#define ZUCSV_NOINLINE
#endif

/* Records between R_CheckUserInterrupt() calls. */
#define ZUCSV_INTERRUPT_INTERVAL 16384

/* Bytes of a cell value shown in an error message before it is elided. */
#define ZUCSV_MSG_MAX 40

/* ------------------------------------------------------------------ *
 * Column types (design SS11)
 * ------------------------------------------------------------------ */

typedef enum {
  ZUCSV_LOGICAL = 0,
  ZUCSV_INTEGER = 1,
  ZUCSV_DOUBLE = 2,
  ZUCSV_CHARACTER = 3
} zucsv_type;

/* ------------------------------------------------------------------ *
 * The na table: each entry is one UTF-8 string a cell may match exactly
 * ------------------------------------------------------------------ */

typedef struct {
  const char **str;
  size_t *len;
  R_xlen_t n;
} zucsv_na;

/* ------------------------------------------------------------------ *
 * Reader state
 *
 * Only stream and parser need manual cleanup; everything else is R_alloc'd
 * and released by R when .Call returns or unwinds (design SS16).
 * ------------------------------------------------------------------ */

typedef struct {
  FILE *stream;
  zsv_parser parser;
  /* Text input: the caller's UTF-8 bytes, borrowed for the duration of the
     .Call. NULL for a path, which is what tells the two apart. Rewinding
     between passes is pos = 0, so a string needs no reopen (design SS9). */
  const char *data;
  size_t len;
  size_t pos;
} zucsv_reader;

/* ------------------------------------------------------------------ *
 * convert.c
 * ------------------------------------------------------------------ */

/* TRUE when the cell matches any entry of the na table. */
int zucsv_is_na(const zucsv_na *na, const unsigned char *str, size_t len);

/* TRUE when the bytes are well-formed UTF-8. */
int zucsv_valid_utf8(const unsigned char *str, size_t len);

/* Index of the first NUL byte, or -1. */
R_xlen_t zucsv_find_nul(const unsigned char *str, size_t len);

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
   path working on (ptr, len), verified bit-identical to as.numeric() and
   independent of LC_NUMERIC; see convert.c. */
int zucsv_as_logical(const unsigned char *str, size_t len);
double zucsv_as_double(const unsigned char *str, size_t len);

/* Fills the conversion tables and chooses the accumulator instance that
   matches R_strtod. Called once from R_init_zucsv(), before any conversion:
   afterwards the conversion state is read-only, which keeps the hot path
   branch-free and would keep it race-free on worker threads. */
void zucsv_numeric_init(void);

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

/* ------------------------------------------------------------------ *
 * read_csv.c
 * ------------------------------------------------------------------ */

SEXP C_read_csv(SEXP file, SEXP text, SEXP header, SEXP delimiter, SEXP na, SEXP col_types);
SEXP C_sniff_csv(SEXP file, SEXP text, SEXP delimiter, SEXP na);

#endif /* ZUCSV_H */
