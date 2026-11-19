# Pins the vendored zsv parser's behavior at the edges zucsv's design depends
# on, observed through read_csv(). tools/zsv-behavior.md explains each one and
# records how it was measured against the parser directly.
#
# If one of these fails after running tools/update-zsv.sh, the backend changed:
# re-read that file and the design's SS10/SS13/SS14/SS15 before "fixing" the
# failure.

test_that("the parser strips a UTF-8 BOM", {
  df <- read_text(c(BOM, charToRaw("a,b\n1,2\n")))
  expect_identical(names(df), c("a", "b"))
  expect_identical(df$a, 1L)

  # a file of nothing but a BOM has no records at all
  expect_identical(dim(read_text(BOM)), c(0L, 0L))
})

test_that("the parser reports quoting per cell", {
  # Everything about SS10's blank-record policy rests on this: without it a
  # quoted empty cell would be indistinguishable from an empty line.
  expect_identical(nrow(read_text("a\n\"\"\nb\n")), 2L)
  expect_identical(nrow(read_text("a\n\nb\n")), 1L)
})

test_that("the parser emits leading blank and empty records, not skips them", {
  # zsv drops every leading all-zero-length record at its default; zucsv
  # sets keep_empty_header_rows = 1 so SS10, not the parser, decides. The
  # record number in an error message is the only place the parser's own
  # emission is observable from R, so that is what pins it: a revert of the
  # option would renumber these, while a data-frame comparison would not
  # notice (tools/zsv-behavior.md).
  expect_error(read_text("\n\na,b\n1,2,3\n"), "CSV row 4 has 3 fields")
  expect_error(read_text("\"\",\"\"\na,b\n1,2,3\n"), "CSV row 3 has 3 fields")
})

test_that("the parser unescapes doubled quotes and keeps embedded delimiters", {
  expect_identical(read_text("a\n\"\"\"\"\n", na = NULL)$a, "\"")
  expect_identical(read_text("a,b\n\"x,y\",2\n")$a, "x,y")
  expect_identical(read_text("a,b\n\"x\ny\",2\n")$a, "x\ny")
})

test_that("a quote never closed is an error naming where it opened (#30)", {
  # The parser raises no status of its own: it closes the cell at end of
  # input as if the quote had been closed. tools/patches/0003 makes it say so
  # through errprintf, and zucsv reports it ahead of any width mismatch.
  msg <- function(row, col, name) {
    sprintf('CSV row %d, column %d ("%s") opens a quote that is never closed', row, col, name)
  }
  cases <- list(
    list("a,b\n\"unterminated,2\n", msg(2, 1, "a")),
    # the last column: once read as one row holding the rest of the file
    list("a,b\n1,\"x\n2,3\n4,5\n", msg(2, 2, "b")),
    list("a,b\r\n1,\"x\r\n2,3\r\n", msg(2, 2, "b")),
    list("a\n\"x\n2\n3\n", msg(2, 1, "a")),
    list("a\n\"x", msg(2, 1, "a")),
    list("a\n\"", msg(2, 1, "a")),
    # an escaped quote is not a closing one
    list("a\n\"ab\"\"", msg(2, 1, "a")),
    # the header record has no names yet, so none is invented
    list("\"a,b\n1,2\n", "CSV row 1, column 1 opens a quote that is never closed"),
    # nor for a cell past the header's width
    list("a,b\n1,2,3,\"x\n", "CSV row 2, column 4 opens a quote that is never closed")
  )
  for (case in cases) {
    expect_error(read_text(case[[1]]), case[[2]], fixed = TRUE, info = case[[1]])
    expect_error(read_csv(text = case[[1]]), case[[2]], fixed = TRUE, info = case[[1]])
    expect_error(sniff_csv(text = case[[1]]), case[[2]], fixed = TRUE, info = case[[1]])
  }
})

test_that("a quote closed by the input's last byte is closed (#30)", {
  expect_identical(read_text("a\n\"x\"")$a, "x")
  expect_identical(read_text("a,b\n1,\"x\ny\"")$b, "x\ny")
  expect_identical(read_csv(text = "a\n\"x\"")$a, "x")
})

test_that("an escaped last cell unescapes without a final newline (#45)", {
  # The closing quote, as the input's last byte, once stayed in the cell.
  # utils::read.csv() is the oracle, with and without the terminator.
  for (cell in c('"x"""', '"x""y"', '""""', '"a""b""c"', '"""x"', '"x""y""z"""')) {
    for (term in c("", "\n", "\r\n")) {
      txt <- paste0("a\n", cell, term)
      want <- utils::read.csv(text = paste0("a\n", cell, "\n"))$a
      expect_identical(read_text(txt, na = NULL)$a, want, info = txt)
      expect_identical(read_csv(text = txt, na = NULL)$a, want, info = txt)
    }
  }
})

test_that("a quote in the middle of an unquoted cell is passed through", {
  # zsv_status_nonstandard_csv is raised only by the fast engine, which
  # zucsv does not use (design SS18).
  expect_identical(read_text("a,b\nx\"y,2\n")$a, "x\"y")
})

test_that("ragged rows reach zucsv rather than being repaired", {
  expect_error(read_text("a,b\n1,2,3\n"), "has 3 fields; expected 2")
  expect_error(read_text("a,b\n1\n"), "has 1 field; expected 2")
})

test_that("quotes at the 8 MiB chunk boundary close and unescape as anywhere else (#30, #45)", {
  skip_unless_stress() # files of 8 MiB
  # The parser reads 8 MiB at a time. A quote that is a chunk's last byte is
  # QUOTE_PENDING until the next chunk; tools/patches/0003 and 0004 depend on
  # that state. Short rows fill the file up to the boundary, so the quote of
  # interest lands at byte `chunk + shift` in a record well under the limit.
  chunk <- 8L * 1024L * 1024L
  filler <- function(n) {
    # rows of exactly n bytes in all, n >= 3
    if (n %% 2L) paste0("yy\n", strrep("y\n", (n - 3L) %/% 2L)) else strrep("y\n", n %/% 2L)
  }
  for (shift in -2:2) {
    # the quote at offset q = nchar(head) + nchar('"x') is byte chunk + shift
    head <- paste0("a\n", filler(chunk + shift - 2L - 2L))
    expect_identical(nchar(head) + 2L, chunk + shift)
    tail_of <- function(df) df$a[nrow(df)]

    closed <- with_big_file(paste0(head, "\"x\"\nb\n"), function(p) read_csv(p))
    expect_identical(closed$a[nrow(closed) - 1L], "x", info = shift)
    expect_identical(tail_of(closed), "b", info = shift)

    at_end <- with_big_file(paste0(head, "\"x\""), function(p) read_csv(p))
    expect_identical(tail_of(at_end), "x", info = shift)

    escaped <- with_big_file(paste0(head, "\"x\"\"y\"\n"), function(p) read_csv(p))
    expect_identical(tail_of(escaped), "x\"y", info = shift)
    escaped_end <- with_big_file(paste0(head, "\"x\"\"\""), function(p) read_csv(p))
    expect_identical(tail_of(escaped_end), "x\"", info = shift)

    expect_error(
      with_big_file(paste0(head, "\"x\"\""), function(p) read_csv(p)),
      "opens a quote that is never closed", fixed = TRUE, info = shift
    )
  }
})
