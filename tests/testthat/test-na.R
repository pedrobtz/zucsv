test_that("the default na turns empty and NA cells into missing values", {
  df <- read_text("a,b\n,NA\n")
  expect_identical(df$a, NA_character_)
  expect_identical(df$b, NA_character_)
})

test_that("matching happens after unquoting, so quoted forms match too", {
  df <- read_text("a,b\n\"\",\"NA\"\n")
  expect_identical(df$a, NA_character_)
  expect_identical(df$b, NA_character_)
})

test_that("na = NULL disables matching", {
  df <- read_text("a,b\n,NA\n", na = NULL)
  expect_identical(df$a, "")
  expect_identical(df$b, "NA")
})

test_that("custom na values replace the default entirely", {
  df <- read_text("a,b,c\n-,NA,\n", na = "-")
  expect_identical(df$a, NA_character_)
  expect_identical(df$b, "NA")
  expect_identical(df$c, "")
})

test_that("matching is exact: no whitespace is trimmed", {
  df <- read_text("a,b\n NA,NA \n")
  expect_identical(df$a, " NA")
  expect_identical(df$b, "NA ")
})

test_that("header names are never matched against na", {
  df <- read_text("NA,b\n1,2\n")
  expect_identical(names(df), c("NA", "b"))
  expect_false(anyNA(names(df)))
})

test_that("na may hold several values, including multi-byte ones", {
  df <- read_text("a,b,c\nnil,none,3\n", na = c("nil", "none"))
  expect_identical(df$a, NA_character_)
  expect_identical(df$b, NA_character_)
  expect_identical(df$c, 3L)
})
