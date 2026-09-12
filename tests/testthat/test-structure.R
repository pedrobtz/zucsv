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
