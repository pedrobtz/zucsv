# Where the semantics overlap, zucsv must agree with utils::read.csv().
#
# Two comparisons, because they answer different questions:
#   * as character, which checks tokenizing, quoting and NA handling without
#     either side's type guessing getting involved;
#   * with both sides inferring, which checks that the type rules agree on
#     input where they are meant to.
#
# Name repair is always switched off: read.csv() mangles names by default and
# zucsv deliberately does not (design SS5).

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

test_that("values agree with utils::read.csv when both read as character", {
  for (txt in canonical) expect_same_text(txt)
})

test_that("inferred types and values agree on ordinary input", {
  for (txt in c(canonical,
                "i,d\n1,1.5\n2,2.5\n",
                "a\nTRUE\nFALSE\n",
                "a\n1e6\n2e6\n",
                "a\n-1\n-2\n",
                "a\n1\nNA\n2\n")) {
    expect_same_inferred(txt)
  }
})

test_that("a write.csv round trip returns the original data frame", {
  set.seed(20260912)
  n <- 300
  df <- data.frame(
    i = sample(-10000:10000, n, replace = TRUE),
    d = round(rnorm(n), 8),
    l = sample(c(TRUE, FALSE), n, replace = TRUE),
    s = replicate(n, paste0(sample(c(letters, " ", ",", '"'), 8, replace = TRUE),
                            collapse = "")),
    stringsAsFactors = FALSE
  )
  path <- tempfile(fileext = ".csv")
  on.exit(unlink(path), add = TRUE)
  utils::write.csv(df, path, row.names = FALSE)

  ours <- read_csv(path)
  expect_identical(names(ours), names(df))
  expect_identical(nrow(ours), nrow(df))
  expect_identical(ours$i, df$i)
  expect_identical(ours$d, df$d)
  expect_identical(ours$l, df$l)
  expect_identical(ours$s, df$s)
})

test_that("Inf and -Inf survive a write.csv round trip", {
  # write.csv() emits these spellings, so they must be double syntax.
  # It writes NaN as NA, so NaN cannot round-trip -- read.csv() loses it too,
  # and agreeing with that is the point of this test.
  df <- data.frame(x = c(Inf, -Inf, NaN, 1.5))
  path <- tempfile(fileext = ".csv")
  on.exit(unlink(path), add = TRUE)
  utils::write.csv(df, path, row.names = FALSE)

  got <- read_csv(path)$x
  expect_type(got, "double")
  expect_identical(got, utils::read.csv(path)$x)
  expect_identical(got, c(Inf, -Inf, NA, 1.5))
})

test_that("a literal NaN in a file reads as NaN, not NA", {
  got <- read_text("x\nNaN\n1.5\n")$x
  expect_true(is.nan(got[1]))
  expect_identical(got[1], as.numeric("NaN"))
  expect_false(identical(got[1], NA_real_))
})
