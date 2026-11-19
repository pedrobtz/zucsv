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
    return(list(file = NULL, text = paste(zucsv_text_utf8(text), collapse = "\n")))
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

# read_csv() and sniff_csv() validate delimiter and na alike; one copy, so a
# fix reaches both (#48). The C side re-checks for native safety (design SS7).
zucsv_check_delimiter <- function(delimiter) {
  if (!is.character(delimiter) || length(delimiter) != 1L || is.na(delimiter)) {
    stop("`delimiter` must be a single character.", call. = FALSE)
  }
  # Bytes, not characters: utf8ToInt() is NA for a string that is not UTF-8,
  # so a one-byte latin1 delimiter once failed with R's own message.
  bytes <- charToRaw(delimiter)
  if (length(bytes) != 1L || as.integer(bytes) > 127L) {
    stop("`delimiter` must be a single ASCII byte.", call. = FALSE)
  }
  if (delimiter %in% c("\n", "\r", "\f", "\"")) {
    stop(
      "`delimiter` may not be a newline, carriage return, form feed or quote.",
      call. = FALSE
    )
  }
  invisible(delimiter)
}

zucsv_check_na <- function(na) {
  if (!is.null(na)) {
    if (!is.character(na)) {
      stop("`na` must be a character vector or NULL.", call. = FALSE)
    }
    if (anyNA(na)) {
      stop("`na` may not contain NA.", call. = FALSE)
    }
  }
  invisible(na)
}

# text elements as UTF-8, before paste(): paste() converts to the native
# encoding, and in a C locale that turned a non-ASCII character into an
# escape such as "<e9>" (#57). Given UTF-8, paste() keeps the bytes.
#
# A declared encoding (latin1, UTF-8) is translated. An unmarked string is in
# the native encoding, so in a Latin-1 locale it is translated too. In any
# other locale its bytes are taken as UTF-8, as a file's are: in a UTF-8
# locale that is what they are, and in a C locale enc2utf8() would escape
# them, so readLines() output read back as "<c3><a9>" (#60). "bytes" means
# raw bytes, taken the same way. C validates every cell as UTF-8, so bytes
# that are not UTF-8 are an error, as in a file.
zucsv_text_utf8 <- function(text) {
  enc <- Encoding(text)
  as_bytes <- enc == "bytes" | (enc == "unknown" & !isTRUE(l10n_info()[["Latin-1"]]))
  out <- text
  if (any(as_bytes)) {
    raw <- text[as_bytes]
    Encoding(raw) <- "UTF-8"
    out[as_bytes] <- raw
  }
  if (any(!as_bytes)) out[!as_bytes] <- enc2utf8(text[!as_bytes])
  out
}
