#' Read a CSV file
#'
#' Reads a delimited text file into a base R data frame, using the bundled
#' `zsv` C parser. The file is read twice: once to establish the shape and
#' validate it, once to fill columns that were allocated at their exact
#' final length.
#'
#' @param file Path to a local file, as a single string. `~` is expanded.
#' @param header If `TRUE` (the default), the first record supplies the
#'   column names. If `FALSE`, every record is data and columns are named
#'   `V1`, `V2`, ... Header values are always text: they are never matched
#'   against `na` and never type-inferred.
#' @param delimiter The field delimiter, as a single ASCII character.
#'   Defaults to `","`. A newline, carriage return, form feed or double
#'   quote is not allowed.
#' @param na Character vector of cell values to read as missing, or `NULL`
#'   to disable missing-value matching. Matching happens after unquoting and
#'   is exact: no whitespace is trimmed, so `NA` and `"NA"` are both missing
#'   under the default but `" NA"` is not.
#' @param col_types `NULL` to infer each column's type, or a character
#'   vector of `"logical"`, `"integer"`, `"double"` or `"character"`. A
#'   single value applies to every column; otherwise give one per column. A
#'   cell that cannot be converted to a forced type is an error, not a
#'   missing value.
#'
#' @details
#' Some behavior worth knowing before you rely on it:
#'
#' * **Blank lines are skipped**, wherever they appear, and do not count as
#'   rows. A line holding only whitespace or only delimiters is *not* blank:
#'   it is a record of empty cells and must match the table's width. In a
#'   single-column file, write an intentional empty cell as `""` --- a bare
#'   empty line is skipped.
#' * **Every record must have the same width.** A mismatch is an error;
#'   short rows are not padded.
#' * **Column names are kept exactly as they appear**, including duplicates
#'   and empty strings. No name repair is performed.
#' * **Text must be valid UTF-8.** A byte sequence that is not, or an
#'   embedded NUL, is an error rather than something passed through to fail
#'   later. A UTF-8 BOM is ignored.
#' * A CR LF pair inside a quoted field is normalised to a single LF.
#'
#' @return A [data.frame] with one column per field. With `header = FALSE`,
#'   or for an empty file, see the notes above for how names and dimensions
#'   are determined. An empty file gives a data frame with zero rows and
#'   zero columns; a header-only file gives zero rows but one column per
#'   header field.
#'
#' @examples
#' path <- tempfile(fileext = ".csv")
#' write.csv(
#'   data.frame(id = 1:3, name = c("Ada", "Linus", "Grace")),
#'   path,
#'   row.names = FALSE
#' )
#'
#' read_csv(path)
#'
#' # Force a column's type rather than inferring it
#' read_csv(path, col_types = c("character", "character"))
#'
#' unlink(path)
#' @export
read_csv <- function(file,
                     header = TRUE,
                     delimiter = ",",
                     na = c("", "NA"),
                     col_types = NULL) {
  if (!is.character(file) || length(file) != 1L || is.na(file)) {
    stop("`file` must be a single non-missing file path.", call. = FALSE)
  }
  if (!is.logical(header) || length(header) != 1L || is.na(header)) {
    stop("`header` must be TRUE or FALSE.", call. = FALSE)
  }
  if (!is.character(delimiter) || length(delimiter) != 1L || is.na(delimiter)) {
    stop("`delimiter` must be a single character.", call. = FALSE)
  }
  if (nchar(delimiter, type = "bytes") != 1L ||
      utf8ToInt(substr(delimiter, 1L, 1L)) > 127L) {
    stop("`delimiter` must be a single ASCII byte.", call. = FALSE)
  }
  if (delimiter %in% c("\n", "\r", "\f", "\"")) {
    stop(
      "`delimiter` may not be a newline, carriage return, form feed or quote.",
      call. = FALSE
    )
  }
  if (!is.null(na)) {
    if (!is.character(na)) {
      stop("`na` must be a character vector or NULL.", call. = FALSE)
    }
    if (anyNA(na)) {
      stop("`na` may not contain NA.", call. = FALSE)
    }
  }
  if (!is.null(col_types)) {
    if (!is.character(col_types) || length(col_types) == 0L || anyNA(col_types)) {
      stop("`col_types` must be a character vector of type names, or NULL.",
           call. = FALSE)
    }
    known <- c("logical", "integer", "double", "character")
    bad <- setdiff(col_types, known)
    if (length(bad) > 0L) {
      stop(
        "`col_types` must contain only ", paste(known, collapse = ", "),
        ". Unknown: ", paste(unique(bad), collapse = ", "), ".",
        call. = FALSE
      )
    }
  }

  file <- path.expand(file)
  # fopen() succeeds on a directory on most Unixes, so catch it here where
  # the message can say what is actually wrong.
  if (dir.exists(file)) {
    stop("`file` is a directory, not a CSV file: ", file, call. = FALSE)
  }

  .Call(C_read_csv, file, header, delimiter, na, col_types)
}
