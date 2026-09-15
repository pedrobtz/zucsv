# Blank records are skipped everywhere (design SS10). The parser does not do
# this itself, so these tests pin zucsv's own policy.

test_that("blank lines are skipped wherever they appear", {
  want <- read_text("a,b\n1,2\n")
  expect_identical(read_text("a,b\n1,2\n\n"), want)      # trailing
  expect_identical(read_text("a,b\n\n1,2\n"), want)      # interior
  expect_identical(read_text("\na,b\n1,2\n"), want)      # leading
  expect_identical(read_text("a,b\n\n\n1,2\n"), want)    # consecutive
  expect_identical(read_text("a,b\r\n\r\n1,2\r\n"), want) # CRLF
})

test_that("a whitespace-only or delimiter-only record is not blank", {
  # one cell of spaces, against a two-column table
  expect_error(read_text("a,b\n   \n1,2\n"), "has 1 field; expected 2")
  # two empty cells: a real record, and it matches the width
  df <- read_text("a,b\n,\n")
  expect_identical(nrow(df), 1L)
  expect_identical(df$a, NA_character_)
})

test_that("in a single-column file a quoted empty cell survives a bare one", {
  # This is the documented escape hatch, and it works because the parser
  # reports per-cell quoting.
  expect_identical(nrow(read_text("a\n\nb\n")), 1L)
  expect_identical(read_text("a\n\nb\n")$a, "b")

  quoted <- read_text("a\n\"\"\nb\n")
  expect_identical(nrow(quoted), 2L)
  expect_identical(quoted$a, c(NA, "b"))
})

test_that("the first record is judged by zucsv's policy, not the parser's", {
  # zsv's own leading-record skipping tests only cell length and ignores
  # quoting, so at its default it swallows any leading record whose cells
  # are all zero-length -- before SS10 can look at it. zucsv sets
  # keep_empty_header_rows so every record arrives.

  # a real header of two empty names, not a dropped record
  empty_names <- read_text(",\n1,2\n")
  expect_identical(names(empty_names), c("", ""))
  expect_identical(nrow(empty_names), 1L)

  # the escape hatch works in leading position too, single- and multi-column
  expect_identical(nrow(read_text("\"\"\n1\n", header = FALSE)), 2L)
  expect_identical(nrow(read_text("\"\",\"\"\nx,y\n", header = FALSE)), 2L)

  # and a leading delimiter-only record still counts as data
  leading <- read_text(",\nx,y\n", header = FALSE)
  expect_identical(nrow(leading), 2L)
  expect_identical(leading$V1, c(NA, "x"))

  # while a bare leading blank line is still skipped
  expect_identical(read_text("\na,b\n1,2\n"), read_text("a,b\n1,2\n"))
})

test_that("skipped blank records still count toward the record number", {
  # SS19 numbers records as the parser emits them, "including the header
  # record and skipped blank records". That is only true because zucsv sets
  # keep_empty_header_rows: at the parser's default the leading blanks are
  # never emitted and the numbering here would be 2, not 4.
  expect_error(read_text("\n\na,b\n1,2,3\n"), "CSV row 4 has 3 fields")

  # interior and trailing blanks have always counted; leading ones now agree
  expect_error(read_text("a,b\n\n1,2,3\n"), "CSV row 3 has 3 fields")
  expect_error(read_text("\na,b\n\n1,2,3\n"), "CSV row 4 has 3 fields")
})

test_that("empty and blank-only files give a 0x0 data frame", {
  for (txt in c("", "\n", "\n\n", "\r\n\r\n")) {
    df <- read_text(txt)
    expect_s3_class(df, "data.frame")
    expect_identical(dim(df), c(0L, 0L))
  }
})

test_that("a header-only file gives zero rows but keeps its columns", {
  df <- read_text("a,b\n")
  expect_identical(dim(df), c(0L, 2L))
  expect_identical(names(df), c("a", "b"))
  expect_identical(df$a, character(0))
  expect_identical(.row_names_info(df), 0L)
})

test_that("header = FALSE on a one-record file gives one row", {
  expect_identical(nrow(read_text("1,2\n", header = FALSE)), 1L)
  expect_identical(dim(read_text("", header = FALSE)), c(0L, 0L))
})
