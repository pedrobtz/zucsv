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
   record (see tools/zsv-behavior.md), which is how it is detected; this
   value only decides where that boundary sits. */
#define ZUCSV_MAX_ROW_SIZE (64u * 1024u * 1024u)

/* Maximum data rows. Stricter than R_XLEN_T_MAX on purpose: a data frame's
   compact row names and nrow() are integers. */
#define ZUCSV_MAX_ROWS 2147483647

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

/* ------------------------------------------------------------------ *
 * read_csv.c
 * ------------------------------------------------------------------ */

SEXP C_read_csv(SEXP file, SEXP header, SEXP delimiter, SEXP na, SEXP col_types);

#endif /* ZUCSV_H */
