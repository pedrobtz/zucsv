# Helpers for test-record-limit.R, kept out of the test file so the suite
# runs shuffled (#29).

record_limit <- 8388608L

oversize_message <- function(row) {
  # sprintf, not paste0: under options(scipen = -5) paste0(3) is "3e+00".
  sprintf("CSV row %d exceeds zucsv's maximum record size of 8388607 bytes", as.integer(row))
}

# Each input is about 9 MB. read_text() leaves its file in tempdir(), which
# for this file would be some 360 MB per run, so these write, read and
# delete one file at a time.
with_big_file <- function(txt, f) {
  path <- csv_file(txt)
  on.exit(unlink(path))
  f(path)
}

read_big <- function(txt, ...) with_big_file(txt, function(p) read_csv(p, ...))

expect_oversize <- function(txt, row) {
  expect_error(read_big(txt), oversize_message(row), fixed = TRUE, info = "file")
  expect_error(read_csv(text = txt), oversize_message(row), fixed = TRUE, info = "text")
}
