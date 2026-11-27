# zucsv_open_stream() (#23, design SS30): the stream holds what read_csv()
# returns for the same file and options -- read_csv() is the oracle, values
# compared with identical(), doubles bit for bit -- and refuses what
# read_csv() refuses, with the same message.

test_that("the stream equals read_csv() for each type, NA and header", {
  inputs <- list(
    "l,i,d,s\nTRUE,1,1.5,a\nfalse,-2147483647,-Inf,\u00e9\nNA,,NaN,\"x,y\"\ntrue,7,1e300,\"\"\n",
    "1,2\n3,4\n",
    "a\n\n1\n\n2\n",
    "\"q\"\"uote\",b\r\n\"line\nbreak\",2\r\n",
    "x;y\n1;2\n"
  )
  for (x in inputs) {
    path <- csv_file(x)
    delim <- if (grepl(";", x, fixed = TRUE)) ";" else ","
    for (header in list(TRUE, FALSE, NA)) {
      expect_stream_matches(path, header = header, delimiter = delim, info = x)
    }
    expect_stream_matches(path, na = NULL, delimiter = delim, info = x)
    expect_stream_matches(text = x, delimiter = delim, info = x)
  }
})

test_that("forced col_types reach the stream as read_csv() applies them", {
  path <- csv_file("a,b,c\n1,2,x\n,4,y\n")
  expect_stream_matches(path, col_types = "character")
  expect_stream_matches(path, col_types = c("double", "integer", "character"))
  expect_stream_matches(path, col_types = c("integer", "double", "character"))
  expect_stream_matches(csv_file("a\nTRUE\n\n"), col_types = "logical")
})

test_that("the schema is a struct of nullable b, i, g and u children", {
  res <- stream_read(csv_file("l,i,d,s\nTRUE,1,1.5,a\n"))
  expect_identical(res$code, 0L)
  expect_identical(res$message, "+s")
  expect_identical(res$formats, c("b", "i", "g", "u"))
  expect_identical(res$names, c("l", "i", "d", "s"))
  # ARROW_FLAG_NULLABLE is 2: every column may hold nulls, a row never is one
  expect_identical(res$schema_flags, c(0, 2, 2, 2, 2))
  expect_identical(
    stream_read(csv_file("1,2\n"), header = FALSE)$names,
    c("V1", "V2")
  )
  expect_identical(
    stream_read(text = "\u00e9t\u00e9\n1\n")$names,
    "\u00e9t\u00e9"
  )
})

test_that("an empty or header-only file is a schema and no batch", {
  for (x in c("", "\n\n", "a,b\n")) {
    res <- stream_read(csv_file(x))
    expect_identical(res$code, 0L, info = x)
    expect_identical(res$batches, list(), info = x)
    expect_identical(stream_df(res), read_csv(csv_file(x)), info = x)
  }
})

test_that("batches split the rows at batch_rows, the last one short", {
  path <- csv_file(paste0("x\n", paste(1:10, collapse = "\n"), "\n"))
  for (b in c(1, 3, 9, 10, 11, 1e6)) {
    res <- stream_read(path, batch_rows = b)
    want <- c(
      rep(min(b, 10), 10 %/% min(b, 10)),
      if (10 %% min(b, 10)) 10 %% min(b, 10)
    )
    expect_identical(stream_rows(res), want, info = b)
    expect_identical(stream_df(res), read_csv(path), info = b)
  }
})

test_that("doubles are as.numeric()'s bit for bit, in every converter mode", {
  x <- strtod_inputs()
  path <- csv_file(paste0("x\n", paste(x, collapse = "\n"), "\n"))
  got <- stream_df(stream_read(path, na = NULL, col_types = "double"))$x
  expect_true(same_bits(got, as.numeric(x)))
  # NaN by name is R's own NaN, sign bit included
  nan <- stream_df(stream_read(text = c("x", "NaN"), col_types = "double"))$x
  expect_true(same_bits(nan, as.numeric("NaN")))
  # each mode agrees with read_csv() in that mode, the R_strtod fallback (2)
  # and long cells included
  long <- c(x, paste0("1.", strrep("3", 400), "e-5"))
  long_path <- csv_file(paste0("x\n", paste(long, collapse = "\n"), "\n"))
  for (mode in 0:4) {
    want <- with_strtod_mode(
      mode,
      read_csv(long_path, na = NULL, col_types = "double")
    )
    got <- stream_df(stream_read(
      long_path,
      na = NULL,
      col_types = "double",
      strtod_mode = mode
    ))
    expect_true(same_bits(got$x, want$x), info = mode)
  }
})

test_that("an error read_csv() raises is the stream's, word for word", {
  bad <- list(
    ragged = "a,b\n1,2,3\n",
    short = "a,b\n1\n",
    nul = c(charToRaw("a\nx"), NUL, charToRaw("y\n")),
    utf8 = c(charToRaw("a\n"), BAD_UTF8, charToRaw("\n")),
    header_utf8 = c(BAD_UTF8, charToRaw("\n1\n")),
    unclosed = "a,b\n1,\"open\n",
    unclosed_header = "a,\"b\n",
    wide = paste(rep("x", 65537L), collapse = ",")
  )
  for (nm in names(bad)) {
    path <- csv_file(bad[[nm]])
    msg <- read_or_message(path)
    expect_type(msg, "character")
    res <- stream_read(path)
    expect_identical(res$code, 22L, info = nm) # EINVAL
    expect_identical(res$message, msg, info = nm)
  }
  forced <- csv_file("a,b\n1,x\n")
  for (types in list(
    "integer",
    c("character", "double"),
    c("integer", "integer", "integer")
  )) {
    msg <- read_or_message(forced, col_types = types)
    res <- stream_read(forced, col_types = types)
    expect_identical(res$code, 22L)
    expect_identical(res$message, msg)
  }
  oversize <- csv_file(paste0("a\n", strrep("x", 8388608), "\n"))
  expect_identical(stream_read(oversize)$message, read_or_message(oversize))
})

test_that("a file that cannot be opened is EIO with read_csv()'s message", {
  missing <- file.path(tempdir(), "zucsv-no-such-file.csv")
  res <- stream_read(missing)
  expect_identical(res$code, 5L) # EIO
  expect_identical(res$message, paste("Cannot open CSV file:", missing))
})

test_that("options read_csv() could not be given are refused", {
  path <- csv_file("a\n1\n")
  refused <- function(...) {
    res <- stream_read(path, ...)
    expect_identical(res$code, 22L)
    res$message
  }
  expect_match(
    refused(strtod_mode = -1),
    "strtod_mode must be set",
    fixed = TRUE
  )
  expect_match(
    refused(strtod_mode = 5),
    "strtod_mode must be set",
    fixed = TRUE
  )
  expect_match(
    refused(batch_rows = 0),
    "batch_rows must be at least 1",
    fixed = TRUE
  )
  expect_match(refused(col_types = "numeric"), "Unknown: numeric", fixed = TRUE)
  expect_match(refused(col_types = character()), "at least one", fixed = TRUE)
  expect_match(refused(delimiter = "\n"), "may not be a newline", fixed = TRUE)
  expect_match(refused(delimiter = "\u00e9"), "single ASCII byte", fixed = TRUE)
})

test_that("a file changed after the stream opened is an error, never values", {
  skip_on_os("windows") # the file is open, so it cannot be rewritten there
  skip_if_not_installed("zuarrow")
  for (after in c("x\n1\n", "x\n1\n2\n3\n4\n", "x,y\n1,2\n")) {
    path <- csv_file("x\n1\n2\n3\n")
    s <- .Call(C_stream_xptr, path, TRUE, ",", c("", "NA"), NULL)
    writeBin(charToRaw(after), path)
    expect_error(
      zuarrow::arrow_to_r(s),
      paste("CSV file changed while it was being read:", path),
      fixed = TRUE
    )
  }
})

test_that("zuarrow validates the stream and reads it as read_csv()", {
  skip_if_not_installed("zuarrow")
  path <- csv_file("l,i,d,s\nTRUE,1,1.5,a\nNA,,NaN,\u00e9\n")
  got <- zuarrow::arrow_to_r(.Call(
    C_stream_xptr,
    path,
    TRUE,
    ",",
    c("", "NA"),
    NULL
  ))
  expect_identical(got, read_csv(path))
  # an error-state stream carries its message to the consumer
  bad <- csv_file("a,b\n1,2,3\n")
  expect_error(
    zuarrow::arrow_to_r(.Call(
      C_stream_xptr,
      bad,
      TRUE,
      ",",
      c("", "NA"),
      NULL
    )),
    "CSV row 2 has 3 fields; expected 2",
    fixed = TRUE
  )
})

test_that("a randomized table round-trips through write.csv() and the stream", {
  withr_seed <- function(seed, code) {
    old <- if (exists(".Random.seed", globalenv())) {
      get(".Random.seed", globalenv())
    }
    on.exit(
      if (is.null(old)) {
        rm(".Random.seed", envir = globalenv())
      } else {
        assign(".Random.seed", old, globalenv())
      }
    )
    set.seed(seed)
    code
  }
  df <- withr_seed(20261127, {
    n <- 300
    data.frame(
      l = sample(c(TRUE, FALSE, NA), n, replace = TRUE),
      i = sample(c(-1e6:1e6, NA), n, replace = TRUE),
      d = ifelse(
        runif(n) < 0.1,
        NA,
        rnorm(n) * 10^sample(-30:30, n, replace = TRUE)
      ),
      s = sample(c("a", "b,c", "\u00e9", "say \"hi\"", NA), n, replace = TRUE),
      stringsAsFactors = FALSE
    )
  })
  path <- tempfile(fileext = ".csv")
  utils::write.csv(df, path, row.names = FALSE)
  expect_stream_matches(path, batch_rows = list(NULL, 64))
  got <- stream_df(stream_read(path))
  expect_identical(got$s, df$s)
  expect_identical(got$l, df$l)
  expect_identical(got$i, as.integer(df$i))
})
