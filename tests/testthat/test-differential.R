# Where the semantics overlap, values must agree with utils::read.csv().
# Type guessing and name repair intentionally differ, so everything is
# compared as character with repair switched off.

base_read <- function(path, ...) {
  utils::read.csv(
    path,
    check.names = FALSE,
    stringsAsFactors = FALSE,
    colClasses = "character",
    na.strings = c("", "NA"),
    ...
  )
}

expect_agrees <- function(text) {
  path <- csv_file(text)
  ours <- read_csv(path)
  theirs <- base_read(path)
  expect_identical(names(ours), names(theirs))
  expect_identical(dim(ours), dim(theirs))
  for (j in seq_along(ours)) {
    expect_identical(ours[[j]], theirs[[j]], info = paste("column", j))
  }
}

test_that("canonical files agree with utils::read.csv", {
  expect_agrees("a,b,c\n1,2,3\n4,5,6\n")
  expect_agrees("id,name\n1,Ada\n2,Linus\n")
  expect_agrees("a,b\n\"x,y\",2\n")
  expect_agrees("a,b\n\"he said \"\"hi\"\"\",2\n")
  expect_agrees("a,b\n\"x\ny\",2\n")
  expect_agrees("a,b\n,\n1,2\n")
  expect_agrees("a,b\n1,2\n")
  expect_agrees("a,b\r\n1,2\r\n")
  expect_agrees("one\n1\n2\n3\n")
})

test_that("a data frame written by write.csv reads back unchanged", {
  set.seed(20260912)
  n <- 200
  df <- data.frame(
    i = as.character(sample(-1000:1000, n, replace = TRUE)),
    d = format(round(rnorm(n), 6), scientific = FALSE, trim = TRUE),
    s = replicate(n, paste0(sample(c(letters, " ", ",", '"'), 8, replace = TRUE),
                            collapse = "")),
    stringsAsFactors = FALSE
  )
  path <- tempfile(fileext = ".csv")
  on.exit(unlink(path), add = TRUE)
  utils::write.csv(df, path, row.names = FALSE)

  ours <- read_csv(path, na = NULL)
  expect_identical(names(ours), names(df))
  expect_identical(nrow(ours), nrow(df))
  expect_identical(ours$s, df$s)
  expect_identical(ours$i, df$i)
})
