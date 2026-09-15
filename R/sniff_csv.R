#' Report how a CSV file would be read
#'
#' Runs the first of [read_csv()]'s two passes and returns what it learned ---
#' the header verdict, the shape, the column names and the inferred column
#' types --- without building any columns. Use it when you want to see the
#' decisions before committing to them, or to pin them down so a later run
#' cannot drift:
#'
#' ```r
#' s <- sniff_csv(path)
#' read_csv(path, header = s$header, col_types = s$col_types)
#' ```
#'
#' That second call reads the file strictly: nothing about it is inferred, so
#' it gives the same answer next month even if the data changes shape.
#'
#' @param file Path to a local file, as a single string. `~` is expanded.
#' @param delimiter The field delimiter, as a single ASCII character.
#'   Defaults to `","`. Not detected --- `zucsv` never guesses the delimiter.
#' @param na Character vector of cell values to read as missing, or `NULL` to
#'   disable missing-value matching. Affects the inferred types, so pass here
#'   whatever you will pass to [read_csv()].
#'
#' @return A list with five elements:
#'   \describe{
#'     \item{`header`}{`TRUE` if the first record is names, `FALSE` if it is
#'       data, by the rule documented under `header` in [read_csv()].}
#'     \item{`ncol`}{Number of columns.}
#'     \item{`nrow`}{Number of data rows --- exact, not an estimate, because
#'       the pass counts every record.}
#'     \item{`col_names`}{The names the file would produce: the first record
#'       when `header` is `TRUE`, otherwise `V1`, `V2`, ...}
#'     \item{`col_types`}{Inferred type of each column, suitable for passing
#'       straight back as `col_types`.}
#'   }
#'   An empty file gives `header = FALSE`, zero columns and zero rows.
#'
#' @details
#' Sniffing reads the whole file and applies exactly the validation
#' [read_csv()] applies, so a ragged row, an embedded NUL or invalid UTF-8 is
#' an error here too. If `sniff_csv()` succeeds, `read_csv()` on the same file
#' will not fail on content.
#'
#' It costs one pass rather than two, but that pass is the expensive one: do
#' not call it before every read as a matter of course. Its purpose is to let
#' you *stop* inferring, not to infer twice.
#'
#' @seealso [read_csv()]
#'
#' @examples
#' path <- tempfile(fileext = ".csv")
#' writeLines(c("id,name,score", "1,Ada,9.5", "2,Grace,8"), path)
#'
#' sniff_csv(path)
#'
#' # Pin the decisions, then read without inferring anything
#' s <- sniff_csv(path)
#' read_csv(path, header = s$header, col_types = s$col_types)
#'
#' unlink(path)
#' @export
sniff_csv <- function(file, delimiter = ",", na = c("", "NA")) {
  if (!is.character(file) || length(file) != 1L || is.na(file)) {
    stop("`file` must be a single non-missing file path.", call. = FALSE)
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

  file <- path.expand(file)
  if (dir.exists(file)) {
    stop("`file` is a directory, not a CSV file: ", file, call. = FALSE)
  }

  .Call(C_sniff_csv, file, delimiter, na)
}
