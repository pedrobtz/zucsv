# Exactly one of `file` and `text` is the input. Returns the two values C
# expects: a path or NULL, and a single string or NULL. A character vector
# given as `text` is joined with newlines, so c("a,b", "1,2") is two lines --
# what utils::read.csv(text=) does, and what anyone passing readLines()
# output will assume.
zucsv_source <- function(file, text) {
  if (is.null(file) && is.null(text)) {
    stop("Supply either `file` or `text`.", call. = FALSE)
  }
  if (!is.null(file) && !is.null(text)) {
    stop("Supply either `file` or `text`, not both.", call. = FALSE)
  }

  if (is.null(file)) {
    if (!is.character(text) || length(text) == 0L || anyNA(text)) {
      stop("`text` must be a character vector with no missing values.",
           call. = FALSE)
    }
    return(list(file = NULL, text = paste(text, collapse = "\n")))
  }

  if (!is.character(file) || length(file) != 1L || is.na(file)) {
    stop("`file` must be a single non-missing file path.", call. = FALSE)
  }
  file <- path.expand(file)
  # fopen() succeeds on a directory on most Unixes, so catch it here where
  # the message can say what is actually wrong.
  if (dir.exists(file)) {
    stop("`file` is a directory, not a CSV file: ", file, call. = FALSE)
  }
  list(file = file, text = NULL)
}
