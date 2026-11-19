# Argument checks, each layer on its own (#34). The R wrappers check for the
# message; C re-checks for native safety (design SS7). The two messages
# differ (R quotes with backticks and ends with a full stop, C with single
# quotes), so matching each exactly shows which layer fired: a test that
# matched both would pass with either guard deleted, which is how seven
# mutants survived the v0.1.1 review.

test_that("the R wrappers reject bad arguments with their own messages", {
  f <- csv_file("a,b\n1,2\n")
  latin1 <- iconv("\u00e9", "UTF-8", "latin1")
  delimiter <- list(
    list(1, "`delimiter` must be a single character."),
    list(c(",", ";"), "`delimiter` must be a single character."),
    list(NA_character_, "`delimiter` must be a single character."),
    list(";;", "`delimiter` must be a single ASCII byte."),
    list("", "`delimiter` must be a single ASCII byte."),
    list("\u00e9", "`delimiter` must be a single ASCII byte."),
    # one byte, but not ASCII (#48)
    list(latin1, "`delimiter` must be a single ASCII byte."),
    list(rawToChar(as.raw(0xe9)), "`delimiter` must be a single ASCII byte."),
    list("\n", "`delimiter` may not be a newline, carriage return, form feed or quote."),
    list("\r", "`delimiter` may not be a newline, carriage return, form feed or quote."),
    list("\f", "`delimiter` may not be a newline, carriage return, form feed or quote."),
    list("\"", "`delimiter` may not be a newline, carriage return, form feed or quote.")
  )
  for (case in delimiter) {
    info <- encodeString(paste(format(case[[1]]), collapse = " "))
    expect_error(read_csv(f, delimiter = case[[1]]), case[[2]], fixed = TRUE, info = info)
    expect_error(sniff_csv(f, delimiter = case[[1]]), case[[2]], fixed = TRUE, info = info)
  }
  for (fn in list(read_csv, sniff_csv)) {
    expect_error(fn(f, na = 1), "`na` must be a character vector or NULL.", fixed = TRUE)
    expect_error(fn(f, na = c("", NA)), "`na` may not contain NA.", fixed = TRUE)
    expect_error(fn(NA_character_), "`file` must be a single non-missing file path.", fixed = TRUE)
    expect_error(fn(c(f, f)), "`file` must be a single non-missing file path.", fixed = TRUE)
  }
  for (header in list(NULL, c(TRUE, FALSE), "yes", 1)) {
    expect_error(read_csv(f, header = header), "`header` must be TRUE, FALSE or NA.", fixed = TRUE)
  }
})

test_that("C rejects what the wrappers would, when called directly", {
  f <- csv_file("a,b\n1,2\n")
  read <- function(file = f, text = NULL, header = TRUE, delimiter = ",",
                   na = "NA", col_types = NULL) {
    .Call(zucsv:::C_read_csv, file, text, header, delimiter, na, col_types)
  }
  sniff <- function(file = f, text = NULL, delimiter = ",", na = "NA") {
    .Call(zucsv:::C_sniff_csv, file, text, delimiter, na)
  }
  expect_error(read(file = NULL), "Supply exactly one of 'file' and 'text'", fixed = TRUE)
  expect_error(read(text = "a\n"), "Supply exactly one of 'file' and 'text'", fixed = TRUE)
  expect_error(read(file = NA_character_), "'file' must be a single non-missing file path", fixed = TRUE)
  expect_error(read(file = 1), "'file' must be a single non-missing file path", fixed = TRUE)
  expect_error(read(file = NULL, text = c("a", "b")), "'text' must be a single non-missing string", fixed = TRUE)
  expect_error(read(file = NULL, text = NA_character_), "'text' must be a single non-missing string", fixed = TRUE)
  expect_error(read(delimiter = 1), "'delimiter' must be a single character", fixed = TRUE)
  expect_error(read(delimiter = NA_character_), "'delimiter' must be a single character", fixed = TRUE)
  expect_error(read(delimiter = ";;"), "'delimiter' must be a single ASCII byte", fixed = TRUE)
  expect_error(read(delimiter = iconv("\u00e9", "UTF-8", "latin1")), "'delimiter' must be a single ASCII byte", fixed = TRUE)
  for (d in c("\n", "\r", "\f", "\"")) {
    expect_error(read(delimiter = d), "'delimiter' may not be a newline, carriage return, form feed or quote", fixed = TRUE)
    expect_error(sniff(delimiter = d), "'delimiter' may not be a newline, carriage return, form feed or quote", fixed = TRUE)
  }
  expect_error(read(na = 1), "'na' must be a character vector or NULL", fixed = TRUE)
  expect_error(read(na = c("", NA)), "'na' may not contain NA", fixed = TRUE)
  expect_error(sniff(na = c("", NA)), "'na' may not contain NA", fixed = TRUE)
  expect_error(read(col_types = 1), "'col_types' must be a character vector or NULL", fixed = TRUE)
  expect_error(read(header = 1), "'header' must be TRUE, FALSE or NA", fixed = TRUE)
  expect_error(read(header = c(TRUE, TRUE)), "'header' must be TRUE, FALSE or NA", fixed = TRUE)
})

test_that("a file that opens but cannot be read is an error, not an empty table", {
  # fopen() succeeds on a directory on Unix, and the first read fails. The R
  # wrapper catches a directory first, so only .Call reaches the C check
  # (read_csv.c, zucsv_check_io).
  skip_on_os("windows")
  dir <- tempfile("dir")
  dir.create(dir)
  on.exit(unlink(dir, recursive = TRUE), add = TRUE)
  expect_error(
    .Call(zucsv:::C_read_csv, dir, NULL, TRUE, ",", "NA", NULL),
    paste0("Cannot read CSV file: ", dir),
    fixed = TRUE
  )
  expect_error(
    .Call(zucsv:::C_sniff_csv, dir, NULL, ",", "NA"),
    paste0("Cannot read CSV file: ", dir),
    fixed = TRUE
  )
  expect_error(read_csv(dir), "is a directory, not a CSV file", fixed = TRUE)
})

test_that("a missing file is an error naming it", {
  path <- file.path(tempdir(), "zucsv-no-such-file.csv")
  expect_error(read_csv(path), paste0("Cannot open CSV file: ", path), fixed = TRUE)
  expect_error(sniff_csv(path), paste0("Cannot open CSV file: ", path), fixed = TRUE)
})
