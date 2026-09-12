test_that("ordinary files read into a data frame", {
  df <- read_text("a,b\n1,2\n", col_types = "character")
  expect_s3_class(df, "data.frame")
  expect_identical(names(df), c("a", "b"))
  expect_identical(df$a, "1")
  expect_identical(dim(df), c(1L, 2L))
  # built directly, so no factors and no name repair
  expect_false(any(vapply(df, is.factor, logical(1))))
})

test_that("row names are the compact form", {
  df <- read_text("a\n1\n2\n3\n")
  expect_identical(.row_names_info(df), -3L)
  expect_identical(rownames(df), c("1", "2", "3"))
})

test_that("header = FALSE generates V1..Vn and keeps every record", {
  df <- read_text("1,2\n3,4\n", header = FALSE, col_types = "character")
  expect_identical(names(df), c("V1", "V2"))
  expect_identical(nrow(df), 2L)
  expect_identical(df$V1, c("1", "3"))
})

test_that("line endings and a missing final terminator are all accepted", {
  want <- read_text("a,b\n1,2\n")
  expect_identical(read_text("a,b\r\n1,2\r\n"), want)
  expect_identical(read_text("a,b\n1,2"), want)
  expect_identical(read_text("a,b\r\n1,2"), want)
})

test_that("single-column files work", {
  df <- read_text("a\n1\n2\n", col_types = "character")
  expect_identical(dim(df), c(2L, 1L))
  expect_identical(df$a, c("1", "2"))
})

test_that("quoting is handled by the parser", {
  expect_identical(read_text("a,b\n\"x,y\",2\n")$a, "x,y")
  expect_identical(read_text("a\n\"he said \"\"hi\"\"\"\n")$a, 'he said "hi"')
  expect_identical(read_text("a,b\n\"x\ny\",2\n")$a, "x\ny")
  # bytes inside a quoted field are passed through untouched, CR included
  expect_identical(read_text("a,b\n\"x\r\ny\",2\n")$a, "x\r\ny")
  expect_identical(read_text("a\n\"x\ry\"\n")$a, "x\ry")
})

test_that("a trailing delimiter produces a final empty field", {
  df <- read_text("a,b,\n1,2,\n")
  expect_identical(ncol(df), 3L)
  expect_identical(names(df), c("a", "b", ""))
  expect_identical(df[[3]], NA_character_)
})

test_that("column names are preserved exactly, including duplicates", {
  expect_identical(names(read_text("a,a\n1,2\n")), c("a", "a"))
  expect_identical(names(read_text("a,,b\n1,2,3\n")), c("a", "", "b"))
})

test_that("delimiters other than comma work", {
  expect_identical(read_text("a;b\n1;2\n", delimiter = ";"), read_text("a,b\n1,2\n"))
  expect_identical(read_text("a\tb\n1\t2\n", delimiter = "\t"), read_text("a,b\n1,2\n"))
  expect_identical(read_text("a|b\n1|2\n", delimiter = "|"), read_text("a,b\n1,2\n"))
})

test_that("long cells survive", {
  long <- strrep("x", 100000)
  expect_identical(read_text(paste0("a,b\n", long, ",2\n"))$a, long)
})

test_that("many rows read correctly", {
  txt <- paste0("a,b\n", paste0(sprintf("%d,x", 1:5000), collapse = "\n"), "\n")
  df <- read_text(txt)
  expect_identical(nrow(df), 5000L)
  expect_identical(df$a[5000], 5000L)
})


test_that("repeated character values are reused without changing anything", {
  # Pass 2 reuses the previous CHARSXP when the bytes match (read_csv.c).
  # These pin that the reuse is invisible: values, NAs, encodings and
  # independence between columns all as if each cell were created fresh.
  runs <- read_text("a,b\nx,1\nx,2\nx,3\ny,4\nx,5\n", col_types = c("character", "integer"))
  expect_identical(runs$a, c("x", "x", "x", "y", "x"))

  # a run interrupted by NA must not let the NA be reused as a value, nor
  # the value before it leak across. The empty cell is written "" because a
  # bare blank line in a one-column file is skipped (design SS10).
  gapped <- read_text("a\nx\n\"\"\nx\nNA\nx\n")
  expect_identical(gapped$a, c("x", NA, "x", NA, "x"))

  # with na = NULL a literal NA cell is an ordinary string, and must not be
  # confused with NA_STRING by the reuse check
  literal <- read_text("a\nNA\nNA\nx\n", na = NULL)
  expect_identical(literal$a, c("NA", "NA", "x"))
  expect_false(anyNA(literal$a))

  # columns keep their own previous value
  two <- read_text("a,b\nx,y\ny,x\nx,y\n")
  expect_identical(two$a, c("x", "y", "x"))
  expect_identical(two$b, c("y", "x", "y"))

  # reuse preserves the UTF-8 marking
  utf <- read_text(c(charToRaw("a\n"), as.raw(c(0xC3, 0xA9)), charToRaw("\n"),
                     as.raw(c(0xC3, 0xA9)), charToRaw("\n")))
  expect_identical(utf$a, c("é", "é"))
  expect_identical(Encoding(utf$a), c("UTF-8", "UTF-8"))

  # values that differ only in length, and only in a trailing byte
  tricky <- read_text("a\nxx\nx\nxx\nxy\nxx\n", na = NULL)
  expect_identical(tricky$a, c("xx", "x", "xx", "xy", "xx"))

  # an empty string next to a longer one, with matching disabled
  empties <- read_text("a\n\"\"\n\"\"\nx\n\"\"\n", na = NULL)
  expect_identical(empties$a, c("", "", "x", ""))
})

test_that("a long run of a long value is still correct", {
  skip_on_cran()
  val <- strrep("abcdefgh", 200)
  txt <- paste0("a\n", paste(rep(val, 5000), collapse = "\n"), "\n")
  got <- read_text(txt)$a
  expect_identical(length(got), 5000L)
  expect_identical(unique(got), val)
})
