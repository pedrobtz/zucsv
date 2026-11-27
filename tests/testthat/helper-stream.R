# The Arrow stream of #23 as a consumer sees it: C_stream_read() opens it
# through the public options and the public entry point, reads every batch
# and converts it in C (src/r_stream.c). Here it becomes a data frame laid
# out as read_csv() lays one out, so the two compare with identical().

stream_read <- function(
  file = NULL,
  text = NULL,
  header = TRUE,
  delimiter = ",",
  na = c("", "NA"),
  col_types = NULL,
  batch_rows = NULL,
  strtod_mode = NULL
) {
  if (!is.null(text)) {
    text <- zucsv_source(NULL, text)$text
  }
  if (!is.null(strtod_mode)) {
    strtod_mode <- as.integer(strtod_mode)
  }
  .Call(
    C_stream_read,
    file,
    text,
    header,
    delimiter,
    na,
    col_types,
    batch_rows,
    strtod_mode
  )
}

stream_empty <- function(format) {
  switch(format, b = logical(), i = integer(), g = double(), u = character())
}

# The batches joined into one data frame, as read_csv() builds it.
stream_df <- function(res) {
  if (!identical(res$code, 0L)) {
    stop("the stream failed: ", res$message)
  }
  cols <- lapply(seq_along(res$names), function(j) {
    parts <- lapply(res$batches, `[[`, j)
    if (length(parts) == 0L) {
      stream_empty(res$formats[[j]])
    } else {
      do.call(c, parts)
    }
  })
  n <- if (length(cols)) length(cols[[1]]) else 0L
  structure(
    cols,
    names = res$names,
    row.names = if (n == 0L) integer() else c(NA_integer_, -n),
    class = "data.frame"
  )
}

stream_rows <- function(res) {
  vapply(res$batches, function(b) attr(b, "rows"), 0)
}

# read_csv()'s value or its error message.
read_or_message <- function(...) {
  tryCatch(read_csv(...), error = conditionMessage)
}

# The stream and read_csv() on the same input, every batch size given.
expect_stream_matches <- function(
  ...,
  batch_rows = list(NULL, 1, 2, 7),
  info = NULL
) {
  want <- read_csv(...)
  for (b in batch_rows) {
    res <- stream_read(..., batch_rows = b)
    got <- stream_df(res)
    expect_identical(got, want, info = paste(info, "batch_rows", format(b)))
    if (!is.null(b) && nrow(want) > 0L) {
      expect_identical(
        stream_rows(res),
        c(rep(b, nrow(want) %/% b), if (nrow(want) %% b) nrow(want) %% b),
        info = info
      )
    }
  }
}
