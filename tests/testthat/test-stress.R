# Larger inputs than the ordinary suite uses. skip_unless_stress() skips them
# on CRAN and under gctorture, and every other leg runs them (see
# helper-fixtures.R, #33). The column cap is in test-errors.R.


test_that("many rows read with the right values at both ends", {
  skip_unless_stress()
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
  skip_unless_stress()
  cell <- strrep("x", 4000000L)
  df <- read_text(paste0("a,b\n", cell, ",2\n"))
  expect_identical(nchar(df$a), 4000000L)
  expect_identical(df$a, cell)
})

test_that("a row over the record-size limit is an error, not a truncation", {
  skip_unless_stress()
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

test_that("many columns and many rows together", {
  skip_unless_stress()
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
