# Forced column types (design SS12). A cell that cannot be converted is an
# error, never a warning followed by NA.

test_that("a single col_types value applies to every column", {
  df <- read_text("a,b\n1,2\n", col_types = "character")
  expect_identical(vapply(df, typeof, ""), c(a = "character", b = "character"))
  expect_identical(df$a, "1")

  df <- read_text("a,b\n1,2\n", col_types = "double")
  expect_identical(vapply(df, typeof, ""), c(a = "double", b = "double"))
})

test_that("one col_types value per column is honoured in order", {
  df <- read_text("a,b,c\n1,x,2.5\n", col_types = c("integer", "character", "double"))
  expect_identical(vapply(df, typeof, ""),
                   c(a = "integer", b = "character", c = "double"))
  expect_identical(df$a, 1L)
  expect_identical(df$c, 2.5)
})

test_that("all four types can be forced", {
  expect_type(read_text("a\nTRUE\n", col_types = "logical")$a, "logical")
  expect_type(read_text("a\n1\n", col_types = "integer")$a, "integer")
  expect_type(read_text("a\n1\n", col_types = "double")$a, "double")
  expect_type(read_text("a\n1\n", col_types = "character")$a, "character")
})

test_that("forcing overrides what inference would have chosen", {
  # would infer integer
  expect_type(read_text("a\n1\n2\n", col_types = "double")$a, "double")
  expect_identical(read_text("a\n1\n2\n", col_types = "character")$a, c("1", "2"))
  # would infer character, because it is all missing
  expect_type(read_text("a\nNA\n", col_types = "integer")$a, "integer")
})

test_that("a cell that does not fit a forced type is an error", {
  expect_error(read_text("a\n1.5\n", col_types = "integer"),
               'Cannot parse row 2, column 1 ("a") as integer: "1.5"', fixed = TRUE)
  expect_error(read_text("a\n12 USD\n", col_types = "double"),
               'Cannot parse row 2, column 1 ("a") as double: "12 USD"', fixed = TRUE)
  expect_error(read_text("a\n1\n", col_types = "logical"),
               'as logical: "1"', fixed = TRUE)
  expect_error(read_text("a\n2147483648\n", col_types = "integer"),
               'as integer: "2147483648"', fixed = TRUE)
})

test_that("the failing error names the right row and column", {
  expect_error(
    read_text("a,b\n1,2\n3,x\n", col_types = c("integer", "integer")),
    'Cannot parse row 3, column 2 ("b") as integer: "x"', fixed = TRUE
  )
  # with header = FALSE the generated name is used
  expect_error(
    read_text("1,x\n", header = FALSE, col_types = "integer"),
    'column 2 ("V2")', fixed = TRUE
  )
})

test_that("a long offending value is truncated in the message", {
  long <- strrep("z", 100)
  err <- tryCatch(read_text(paste0("a\n", long, "\n"), col_types = "double"),
                  error = conditionMessage)
  expect_match(err, "z{40}\\.\\.\\.")
  expect_false(grepl(strrep("z", 41), err))
})

test_that("missing cells are exempt from a forced type", {
  df <- read_text("a\nNA\n1\n", col_types = "integer")
  expect_identical(df$a, c(NA, 1L))
  # a bare blank line is skipped (design SS10), so the empty cell has to be
  # written as "" for there to be one at all
  df <- read_text("a\n\"\"\nTRUE\n", col_types = "logical")
  expect_identical(df$a, c(NA, TRUE))
  # with na = NULL nothing is missing, so that empty cell must convert
  expect_error(read_text("a\n\"\"\n1\n", na = NULL, col_types = "integer"),
               "as integer", fixed = TRUE)
})

test_that("character accepts anything that reached it", {
  expect_identical(read_text("a\nx\n1\nTRUE\n", col_types = "character")$a,
                   c("x", "1", "TRUE"))
})

test_that("a header is never subject to col_types", {
  df <- read_text("1,2\n3,4\n", col_types = "integer")
  expect_identical(names(df), c("1", "2"))
  expect_identical(df[[1]], 3L)
})
