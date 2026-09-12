# Pins the vendored zsv parser's behavior at the edges zucsv's design depends
# on. tools/zsv-behavior.md explains each expectation; if one of these fails
# after running tools/update-zsv.sh, the backend changed and that file and the
# design's SS10/SS13/SS15 need re-reading before the failure is "fixed".

# Fixtures are written as raw bytes so line endings, BOMs and NULs are
# exact, and so this file stays pure ASCII.
write_csv_bytes <- function(text) {
  path <- tempfile(fileext = ".csv")
  con <- file(path, "wb")
  on.exit(close(con))
  writeBin(if (is.raw(text)) text else charToRaw(text), con)
  path
}

BOM <- as.raw(c(0xEF, 0xBB, 0xBF))

smoke <- function(text) {
  .Call(C_zsv_smoke, write_csv_bytes(text))
}

test_that("the parser reads ordinary records", {
  expect_equal(smoke("a,b\n1,2\n"), c(records = 2, cells = 4))
  expect_equal(smoke("a,b\n1,2"), c(records = 2, cells = 4))
  expect_equal(smoke("a,b\r\n1,2\r\n"), c(records = 2, cells = 4))
  expect_equal(smoke("a,b\n"), c(records = 1, cells = 2))
  expect_equal(smoke(""), c(records = 0, cells = 0))
})

test_that("blank lines arrive as one-cell records, except a leading one", {
  # The parser does not skip them; zucsv's SS10 policy does that itself.
  expect_equal(smoke("a,b\n1,2\n\n"), c(records = 3, cells = 5))
  expect_equal(smoke("a,b\n\n1,2\n"), c(records = 3, cells = 5))
  expect_equal(smoke("a,b\n\n\n1,2\n"), c(records = 4, cells = 6))
  # A blank before the header is swallowed (keep_empty_header_rows = 0).
  expect_equal(smoke("\na,b\n1,2\n"), c(records = 2, cells = 4))
  expect_equal(smoke("\n\n"), c(records = 0, cells = 0))
})

test_that("quoting, BOM and ragged rows behave as recorded", {
  expect_equal(smoke("a,b\n\"x,y\",2\n"), c(records = 2, cells = 4))
  expect_equal(smoke("a,b\n\"x\ny\",2\n"), c(records = 2, cells = 4))
  expect_equal(smoke(c(BOM, charToRaw("a,b\n1,2\n"))), c(records = 2, cells = 4))
  expect_equal(smoke("a,b\n1,2,3\n"), c(records = 2, cells = 5))
  expect_equal(smoke("a,b\n1\n"), c(records = 2, cells = 3))
  # An unbalanced quote is not a parser error: the rest of the line becomes
  # one cell, which zucsv reports as a row-width mismatch.
  expect_equal(smoke("a,b\n\"unterminated,2\n"), c(records = 2, cells = 3))
})

test_that("an oversize row is silently emitted as a zero-cell record", {
  # This is the only way zucsv can detect the overflow, so it must stay true.
  big <- paste0("a,b\n", strrep("x", 500000), ",2\n")
  expect_equal(smoke(big), c(records = 2, cells = 2))
})
