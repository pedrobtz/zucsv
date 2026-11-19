# Helpers and fixtures for test-differential.R. They live here, not at the
# top of the test file, because devtools::test(shuffle = TRUE) reorders a
# test file's top-level code (#29).

base_read <- function(path, ...) {
  utils::read.csv(path, check.names = FALSE, stringsAsFactors = FALSE,
                  na.strings = c("", "NA"), ...)
}

expect_same_text <- function(text) {
  path <- csv_file(text)
  ours <- read_csv(path, col_types = "character")
  theirs <- base_read(path, colClasses = "character")
  expect_identical(names(ours), names(theirs), info = text)
  expect_identical(dim(ours), dim(theirs), info = text)
  for (j in seq_along(ours)) {
    expect_identical(ours[[j]], theirs[[j]], info = paste(text, "column", j))
  }
}

expect_same_inferred <- function(text) {
  path <- csv_file(text)
  ours <- read_csv(path)
  theirs <- base_read(path)
  expect_identical(vapply(ours, typeof, ""), vapply(theirs, typeof, ""),
                   info = text)
  for (j in seq_along(ours)) {
    expect_identical(ours[[j]], theirs[[j]], info = paste(text, "column", j))
  }
}

canonical <- c(
  "a,b,c\n1,2,3\n4,5,6\n",
  "id,name\n1,Ada\n2,Linus\n",
  "a,b\n\"x,y\",2\n",
  "a,b\n\"he said \"\"hi\"\"\",2\n",
  "a,b\n\"x\ny\",2\n",
  "a,b\n,\n1,2\n",
  "a,b\r\n1,2\r\n",
  "one\n1\n2\n3\n"
)
