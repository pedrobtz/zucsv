# Larger inputs than the ordinary suite uses. All skipped on CRAN, where the
# time budget is small; the sanitizer job runs them with NOT_CRAN set, which
# is where they earn their keep.

test_that("a wide table at the column cap reads, and one past it errors", {
  skip_on_cran()
  at_cap <- paste0(paste0("c", seq_len(65536L)), collapse = ",")
  df <- read_text(paste0(at_cap, "\n"))
  expect_identical(ncol(df), 65536L)
  expect_identical(nrow(df), 0L)

  over <- paste0(at_cap, ",c65537\n")
  expect_error(read_text(over), "exceeds zucsv's maximum of 65536 columns", fixed = TRUE)
})

test_that("many rows read with the right values at both ends", {
  skip_on_cran()
  n <- 200000L
  txt <- paste0("i,s\n", paste0(seq_len(n), ",x", collapse = "\n"), "\n")
  df <- read_text(txt)
  expect_identical(nrow(df), n)
  expect_type(df$i, "integer")
  expect_identical(df$i[1], 1L)
  expect_identical(df$i[n], n)
  expect_identical(unique(df$s), "x")
})

test_that("a multi-megabyte single cell survives", {
  skip_on_cran()
  cell <- strrep("x", 4000000L)
  df <- read_text(paste0("a,b\n", cell, ",2\n"))
  expect_identical(nchar(df$a), 4000000L)
  expect_identical(df$a, cell)
})

test_that("a row over the record-size limit is an error, not a truncation", {
  skip_on_cran()
  # The parser drops an oversize row silently, as a zero-cell record, so
  # without zucsv's own check this would read as a short table rather than
  # fail (design SS15, tools/zsv-behavior.md).
  big <- strrep("y", 12000000L)
  expect_error(
    read_text(paste0("a,b\n", big, ",2\n")),
    "exceeds zucsv's maximum record size",
    fixed = TRUE
  )
})

test_that("repeated failing parses do not accumulate anything", {
  skip_on_cran()
  # Under the sanitizer job this is the leak check for the error path.
  bad <- csv_file("a,b\n1,2,3\n")
  for (i in 1:2000) expect_error(read_csv(bad), "has 3 fields")

  # and repeated successful ones
  good <- csv_file("a,b\n1,2\n3,4\n")
  for (i in 1:2000) expect_identical(nrow(read_csv(good)), 2L)
})

test_that("many columns and many rows together", {
  skip_on_cran()
  ncol <- 500L
  nrow <- 500L
  hdr <- paste0(paste0("c", seq_len(ncol)), collapse = ",")
  row <- paste0(seq_len(ncol), collapse = ",")
  txt <- paste0(hdr, "\n", paste(rep(row, nrow), collapse = "\n"), "\n")
  df <- read_text(txt)
  expect_identical(dim(df), c(nrow, ncol))
  expect_type(df[[ncol]], "integer")
  expect_identical(df[[ncol]][1], ncol)
})
