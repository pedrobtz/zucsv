test_that("UTF-8 cell values and names round-trip as UTF-8", {
  # e-acute (C3 A9) in a value and in a header name
  bytes <- c(charToRaw("id,"), as.raw(c(0xC3, 0xA9)), charToRaw("\n1,"),
             as.raw(c(0xC3, 0xA9)), charToRaw("\n"))
  df <- read_text(bytes)
  expect_identical(Encoding(names(df)[2]), "UTF-8")
  expect_identical(Encoding(df[[2]]), "UTF-8")
  expect_identical(nchar(df[[2]]), 1L)
  expect_identical(charToRaw(df[[2]]), as.raw(c(0xC3, 0xA9)))
})

test_that("a UTF-8 BOM is ignored, leaving the first name intact", {
  df <- read_text(c(BOM, charToRaw("a,b\n1,2\n")))
  expect_identical(names(df), c("a", "b"))
})

test_that("invalid UTF-8 is an error naming the location", {
  expect_error(
    read_text(c(charToRaw("a,b\n"), BAD_UTF8, charToRaw(",2\n"))),
    'invalid UTF-8 at row 2, column 1 ("a")',
    fixed = TRUE
  )
  # in a header there is no column name yet, so none is quoted
  expect_error(
    read_text(c(BAD_UTF8, charToRaw(",b\n1,2\n"))),
    "invalid UTF-8 at row 1, column 1",
    fixed = TRUE
  )
})

test_that("an embedded NUL is an error naming the location", {
  expect_error(
    read_text(c(charToRaw("a,b\n1"), NUL, charToRaw("x,3\n"))),
    'embedded NUL at row 2, column 1 ("a")',
    fixed = TRUE
  )
  expect_error(
    read_text(c(charToRaw("a"), NUL, charToRaw("z,b\n1,2\n"))),
    "embedded NUL at row 1, column 1",
    fixed = TRUE
  )
})

test_that("a path with non-ASCII characters and ~ are both handled", {
  dir <- file.path(tempdir(), "zucsv-éà")
  dir.create(dir, showWarnings = FALSE)
  on.exit(unlink(dir, recursive = TRUE), add = TRUE)
  path <- file.path(dir, "café.csv")
  con <- file(path, "wb"); writeBin(charToRaw("a,b\n1,2\n"), con); close(con)

  expect_identical(read_csv(path)$a, 1L)
  # path.expand() is applied, so a ~ path resolves
  expect_identical(read_csv(path.expand(path))$a, 1L)
})


test_that("byte checks still fire in columns that look numeric or logical", {
  # The text checks are skipped for any cell a grammar accepts, since the
  # grammars take only ASCII. These are the cells that must still be caught
  # because no grammar accepts them.
  expect_error(read_text(c(charToRaw("a\n1\n"), NUL, charToRaw("\n3\n"))),
               "embedded NUL at row 3, column 1", fixed = TRUE)
  expect_error(read_text(c(charToRaw("a\n1\n"), BAD_UTF8, charToRaw("\n3\n"))),
               "invalid UTF-8 at row 3, column 1", fixed = TRUE)
  expect_error(read_text(c(charToRaw("a\n1\n2"), NUL, charToRaw("\n"))),
               "embedded NUL at row 3, column 1", fixed = TRUE)
  expect_error(read_text(c(charToRaw("a\nTRUE\n"), BAD_UTF8, charToRaw("\nFALSE\n"))),
               "invalid UTF-8 at row 3, column 1", fixed = TRUE)
})

test_that("a byte problem is reported in preference to a conversion failure", {
  # Both apply to these cells; the byte message says more about the file.
  expect_error(read_text(c(charToRaw("a\n1\n"), NUL, charToRaw("\n")), col_types = "double"),
               "embedded NUL at row 3, column 1", fixed = TRUE)
  expect_error(read_text(c(charToRaw("a\n1\n"), BAD_UTF8, charToRaw("\n")), col_types = "double"),
               "invalid UTF-8 at row 3, column 1", fixed = TRUE)
  expect_error(read_text(c(charToRaw("a\nx\n"), NUL, charToRaw("\n")), col_types = "character"),
               "embedded NUL at row 3, column 1", fixed = TRUE)
})

test_that("na matching does not let a bad cell through unchecked", {
  expect_error(read_text(c(charToRaw("a\nx\n"), NUL, charToRaw("\n")), na = NULL),
               "embedded NUL at row 3, column 1", fixed = TRUE)
})

test_that("valid UTF-8 beside numbers still reads", {
  df <- read_text(c(charToRaw("a\n1\n"), as.raw(c(0xC3, 0xA9)), charToRaw("\n")))
  expect_identical(df$a, c("1", "\u00e9"))
  expect_identical(Encoding(df$a[2]), "UTF-8")
})
