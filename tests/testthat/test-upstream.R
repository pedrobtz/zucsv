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

test_that("an unbalanced quote is not a parser error but a width mismatch", {
  # The parser swallows the rest of the line into one cell and reports no
  # error of its own, so zucsv sees a short row.
  expect_error(read_text("a,b\n\"unterminated,2\n"), "has 1 field; expected 2")
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
