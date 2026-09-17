# text = is the same reader over the same bytes, so the contract these tests
# defend is equivalence with the file path: anything read_csv() does to a
# file it must do to a string, and the differences are only the ones a string
# makes impossible.

test_that("text = reads the same table as the same bytes in a file", {
  csv <- "a,b\n1,2\n3,4\n"
  expect_identical(read_csv(text = csv), read_text(csv))
})

test_that("a character vector is one line per element", {
  expect_identical(
    read_csv(text = c("id,name", "1,Ada", "2,Linus")),
    read_text("id,name\n1,Ada\n2,Linus\n")
  )
})

test_that("readLines() output reads back unchanged", {
  path <- csv_file("a,b\n1,2\n3,4\n")
  expect_identical(read_csv(text = readLines(path)), read_csv(path))
})

test_that("exactly one source is required", {
  path <- csv_file("a\n1\n")
  expect_error(read_csv(), "Supply either `file` or `text`")
  expect_error(read_csv(path, text = "a\n1\n"), "not both")
  expect_error(sniff_csv(), "Supply either `file` or `text`")
  expect_error(sniff_csv(path, text = "a\n1\n"), "not both")
})

test_that("text must be a character vector with no missing values", {
  expect_error(read_csv(text = NA_character_), "no missing values")
  expect_error(read_csv(text = c("a", NA)), "no missing values")
  expect_error(read_csv(text = 1L), "must be a character vector")
  expect_error(read_csv(text = character(0)), "must be a character vector")
})

test_that("every argument behaves as it does for a file", {
  expect_identical(
    read_csv(text = "a;b\n1;2", delimiter = ";"),
    read_text("a;b\n1;2", delimiter = ";")
  )
  expect_identical(
    read_csv(text = "a,b\n1,.", na = "."),
    read_text("a,b\n1,.", na = ".")
  )
  expect_identical(
    read_csv(text = "1,2\n3,4", header = FALSE),
    read_text("1,2\n3,4", header = FALSE)
  )
  expect_identical(
    read_csv(text = "a,b\n1,2", col_types = "character"),
    read_text("a,b\n1,2", col_types = "character")
  )
  expect_identical(
    read_csv(text = "1,2\n3,4", header = NA),
    read_text("1,2\n3,4", header = NA)
  )
})

test_that("structural features survive the string", {
  # quoted field with an embedded newline and an escaped quote
  expect_identical(
    read_csv(text = "a,b\n\"x\ny\",\"he said \"\"hi\"\"\"\n"),
    read_text("a,b\n\"x\ny\",\"he said \"\"hi\"\"\"\n")
  )
  # CRLF, and a missing final terminator
  expect_identical(read_csv(text = "a,b\r\n1,2\r\n"), read_csv(text = "a,b\n1,2"))
  # blank records are skipped, as in a file
  expect_identical(read_csv(text = "a\n\n1\n\n"), read_text("a\n\n1\n\n"))
})

test_that("a UTF-8 BOM is ignored, as it is in a file", {
  bom <- rawToChar(BOM)
  Encoding(bom) <- "UTF-8"
  expect_identical(read_csv(text = paste0(bom, "a,b\n1,2")), read_csv(text = "a,b\n1,2"))
})

test_that("an empty string gives the empty data frame", {
  expect_identical(read_csv(text = ""), read_text(""))
  expect_identical(dim(read_csv(text = "")), c(0L, 0L))
})

test_that("errors carry row and column, and name no file", {
  err <- expect_error(
    read_csv(text = "a,b\n1,x\n", col_types = c("integer", "integer"))
  )
  expect_match(conditionMessage(err), "row 2, column 2")
  expect_match(conditionMessage(err), '"x"')

  expect_error(read_csv(text = "a,b\n1\n"), "row 2 has 1 field; expected 2")
})

test_that("a declared encoding is honoured, where a file's cannot be", {
  # The same byte is an error in a file, because a file carries no encoding.
  latin1 <- rawToChar(as.raw(c(0x61, 0x2C, 0x62, 0x0A, 0x31, 0x2C, 0xE9)))
  Encoding(latin1) <- "latin1"
  df <- read_csv(text = latin1)
  expect_identical(df$b, "é")
  expect_identical(Encoding(df$b), "UTF-8")

  expect_error(read_text(as.raw(c(0x61, 0x0A, 0xE9))), "invalid UTF-8")
})

test_that("sniff_csv(text =) describes the same read", {
  csv <- "a,b\n1,2.5\n3,4.5\n"
  expect_identical(sniff_csv(text = csv), sniff_csv(csv_file(csv)))

  s <- sniff_csv(text = csv)
  expect_identical(
    read_csv(text = csv, header = s$header, col_types = s$col_types),
    read_csv(text = csv)
  )
})

test_that("a string is read twice without being consumed", {
  # Both passes run over the same buffer, so a second read of the same string
  # must give the same answer -- the rewind is pos = 0, not a reopen.
  csv <- "a\n1\n2\n3\n"
  expect_identical(read_csv(text = csv), read_csv(text = csv))
  expect_identical(nrow(read_csv(text = csv)), 3L)
})
