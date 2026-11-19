# The record-size limit (design SS15, #28). A record is its bytes between
# terminators, delimiters and quotes included; the largest that reads is
# ZUCSV_MAX_ROW_SIZE - 1 = 8388607 bytes. zsv reports an overflow only as a
# diagnostic, and hands over the cells that fit and then the rest of the line
# as further records, so every shape below once read as a wrong table.
#
# Ungated on purpose: each read takes milliseconds, and these are the inputs
# the sanitizer and valgrind legs most need to see.

test_that("an oversize record is an error wherever it overflows (#28)", {
  x <- function(n) strrep("X", n)
  # first cell: the only shape the parser reports as a zero-cell record
  expect_oversize(paste0("a\n1\n", x(9e6), "\n3\n"), 3)
  # a later cell: read as a short row plus a made-up row
  expect_oversize(paste0("a\n1\n2,", x(record_limit + 92), "\n3\n"), 3)
  expect_oversize(paste0("a,b\n1,2,", x(9e6), "\n3,4\n"), 2)
  expect_oversize(paste0("a,b\n1,2\n3,4,", x(9e6), "\n"), 3)
  # read as nothing at all
  expect_oversize(paste0("a\n1\n2,", x(record_limit), "\n3\n"), 3)
  # read as an embedded NUL the input does not have
  expect_oversize(paste0("a\n1\n2,", x(record_limit - 1), "\n3\n"), 3)
  # no final terminator
  expect_oversize(paste0("a\n", x(9e6)), 2)
  expect_oversize(paste0("a,b\n1,2\n3,", x(9e6)), 3)
  expect_oversize(x(9e6), 1)
  # quoted, CRLF, and the header record
  expect_oversize(paste0("a,b\n1,\"", x(9e6), "\"\n2,3\n"), 2)
  expect_oversize(paste0("a,b\n\"", x(9e6), "\",1\n2,3\n"), 2)
  expect_oversize(paste0("a,b\r\n1,", x(9e6), "\r\n2,3\r\n"), 2)
  expect_oversize(paste0("a,", x(9e6), "\n1,2\n"), 1)
})

test_that("sniff_csv() reports an oversize record as read_csv() does (#28)", {
  txt <- paste0("a\n1\n2,", strrep("X", record_limit + 92), "\n3\n")
  expect_error(with_big_file(txt, sniff_csv), oversize_message(3), fixed = TRUE)
  expect_error(sniff_csv(text = txt), oversize_message(3), fixed = TRUE)
})

test_that("the record limit is 8388607 bytes on every terminator (#28)", {
  for (term in c("\n", "\r\n", "")) {
    fits <- strrep("X", record_limit - 1L)
    df <- read_big(paste0("a\n", fits, term))
    expect_identical(df$a, fits, info = encodeString(term))
    expect_identical(read_csv(text = paste0("a\n", fits, term))$a, fits)
    expect_oversize(paste0("a\n", strrep("X", record_limit), term), 2)
    expect_oversize(paste0("a\n", strrep("X", record_limit + 1L), term), 2)
  }
})

test_that("delimiters and quotes count toward the record limit (#28)", {
  x <- function(n) strrep("X", n)
  two <- read_big(paste0("a,b\n1,", x(record_limit - 3L), "\n"))
  expect_identical(nchar(two$b), record_limit - 3L)
  expect_oversize(paste0("a,b\n1,", x(record_limit - 2L), "\n"), 2)
  quoted <- read_big(paste0("a\n\"", x(record_limit - 3L), "\"\n"))
  expect_identical(nchar(quoted$a), record_limit - 3L)
  expect_oversize(paste0("a\n\"", x(record_limit - 2L), "\"\n"), 2)
})
