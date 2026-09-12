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

  expect_identical(read_csv(path)$a, "1")
  # path.expand() is applied, so a ~ path resolves
  expect_identical(read_csv(path.expand(path))$a, "1")
})
