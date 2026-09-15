# header = NA: the first record is names unless one of its fields is numeric
# or logical syntax (design SS4, SS29 decision 20). The rule is deliberately
# narrower than fread's and DuckDB's -- these tests pin down both what it
# gets right and the one case it gives up.

test_that("a record of names is detected as a header", {
  df <- read_text("a,b,c\n1,2,3\n4,5,6\n", header = NA)
  expect_identical(names(df), c("a", "b", "c"))
  expect_identical(nrow(df), 2L)
})

test_that("a record of numbers is detected as data", {
  df <- read_text("1,2,3\n4,5,6\n", header = NA)
  expect_identical(names(df), c("V1", "V2", "V3"))
  expect_identical(nrow(df), 2L)
  expect_identical(df$V1, c(1L, 4L))
})

test_that("one numeric field is enough to make the record data", {
  # Exactly one field of record 1 is numeric syntax; every other field would
  # pass for a name. A rule that needed two would read this as a header.
  one <- read_text("01101,hokkaido,x\n01102,aomori,y\n", header = NA)
  expect_identical(names(one), c("V1", "V2", "V3"))
  expect_identical(nrow(one), 2L)

  # the same shape with the numeric field last, so position cannot matter
  last <- read_text("code,hokkaido,0\nname,aomori,1\n", header = NA)
  expect_identical(names(last), c("V1", "V2", "V3"))
  expect_identical(nrow(last), 2L)

  # KEN_ALL itself, where the leading-zero code is integer syntax (decision 4)
  expect_identical(names(read_text("01101,hokkaido,0\n01102,aomori,1\n",
                                   header = NA)),
                   c("V1", "V2", "V3"))
})

test_that("a quoted field always counts as a name", {
  # Quoting is the writer saying "this is text" (SS29 decision 21).
  df <- read_text("\"id\",\"2024\",\"2025\"\n1,5,6\n2,7,8\n", header = NA)
  expect_identical(names(df), c("id", "2024", "2025"))
  expect_identical(nrow(df), 2L)
  # one bare numeric field still settles it, quoted neighbours or not
  expect_identical(names(read_text("\"id\",2024\nx,1\n", header = NA)),
                   c("V1", "V2"))

  # quoting outranks the logical grammar too, not just the numeric one
  quoted_logical <- read_text("\"TRUE\",\"FALSE\"\nx,y\n", header = NA)
  expect_identical(names(quoted_logical), c("TRUE", "FALSE"))
  expect_identical(nrow(quoted_logical), 1L)
  # unquoted, the same spellings are data
  expect_identical(names(read_text("TRUE,FALSE\nx,y\n", header = NA)),
                   c("V1", "V2"))
})

test_that("the three divergences from fread and DuckDB are pinned", {
  # Deciding from one record reaches a different verdict than comparing
  # against the rest of the file (SS29 decisions 20 and 21). Each case below
  # also asserts which *direction* the row count moves, because that is the
  # thing the documentation states and it is not the same for all three.

  # 1. an unquoted header of numeric-looking names over numeric columns:
  #    read as data, so the name record becomes a row -- one row MORE
  unquoted_years <- read_text("id,2024,2025\n1,5,6\n2,7,8\n", header = NA)
  expect_identical(names(unquoted_years), c("V1", "V2", "V3"))
  expect_identical(nrow(unquoted_years), 3L)
  expect_identical(
    nrow(unquoted_years),
    nrow(read_text("id,2024,2025\n1,5,6\n2,7,8\n", header = TRUE)) + 1L
  )

  # 2. a first record that is entirely missing values: read as a header,
  #    so a data row is consumed -- one row FEWER
  all_na <- read_text("NA,NA,NA\n1,2,3\n", header = NA)
  expect_identical(names(all_na), c("NA", "NA", "NA"))
  expect_identical(nrow(all_na), 1L)
  expect_identical(
    nrow(all_na),
    nrow(read_text("NA,NA,NA\n1,2,3\n", header = FALSE)) - 1L
  )

  # 3. a headerless first record of entirely quoted numbers
  all_quoted <- read_text("\"1\",\"2\"\n\"3\",\"4\"\n", header = NA)
  expect_identical(names(all_quoted), c("1", "2"))
  expect_identical(nrow(all_quoted), 1L)
})

test_that("logical and special-double spellings count as data", {
  expect_identical(names(read_text("TRUE,FALSE\nTRUE,TRUE\n", header = NA)),
                   c("V1", "V2"))
  expect_identical(names(read_text("Inf,x\n1,y\n", header = NA)), c("V1", "V2"))
  expect_identical(names(read_text("NaN,x\n1,y\n", header = NA)), c("V1", "V2"))
  # T and F are not logical syntax in v0.1, so they read as names
  expect_identical(names(read_text("T,F\nx,y\n", header = NA)), c("T", "F"))
})

test_that("an all-character file is read as having a header", {
  # Nothing in the file distinguishes the cases; fread and DuckDB guess the
  # same way. Documented, not accidental.
  df <- read_text("x,y,z\nx,y,z\nx,y,z\n", header = NA)
  expect_identical(names(df), c("x", "y", "z"))
  expect_identical(nrow(df), 2L)
})

test_that("the documented divergence from fread and DuckDB is data", {
  # fread and DuckDB compare against the rest of the file and call this a
  # header; deciding from the first record alone cannot (SS29 decision 20).
  df <- read_text("1,score\n5,10\n6,20\n", header = NA)
  expect_identical(names(df), c("V1", "V2"))
  expect_identical(nrow(df), 3L)
})

test_that("empty and na-looking fields do not make a record data", {
  df <- read_text("a,,c\n1,2,3\n", header = NA)
  expect_identical(names(df), c("a", "", "c"))
  expect_identical(nrow(df), 1L)
  # `NA` is neither numeric nor logical syntax, and detection runs before
  # na matching, which never applies to a header anyway
  expect_identical(names(read_text("NA,b\n1,2\n", header = NA)), c("NA", "b"))
})

test_that("detection does not consult na", {
  # A declared missing value still votes on its own syntax. `na` is excluded
  # deliberately: "" is in the default na set, so letting it vote would read
  # the ordinary header `a,,c` as data (SS4).
  df <- read_text("1,b\nx,y\n", header = NA, na = "1")
  expect_identical(names(df), c("V1", "V2"))
  expect_identical(nrow(df), 2L)
  expect_identical(df$V1, c(NA, "x"))

  # and the reverse: declaring a name missing does not make it data
  expect_identical(names(read_text("a,b\n1,2\n", header = NA, na = "a")),
                   c("a", "b"))
  # the case that forces the rule
  expect_identical(names(read_text("a,,c\n1,2,3\n", header = NA,
                                   na = c("", "NA"))),
                   c("a", "", "c"))
})

test_that("detection does not consult col_types", {
  # A forced type cannot contradict the verdict: both calls see the same
  # first record and must agree about it.
  txt <- "a,b\n1,2\n"
  expect_identical(names(read_text(txt, header = NA)), c("a", "b"))
  expect_identical(names(read_text(txt, header = NA, col_types = "character")),
                   c("a", "b"))
  expect_identical(names(read_text("1,2\n3,4\n", header = NA,
                                   col_types = "character")),
                   c("V1", "V2"))
})

test_that("a detected header keeps its names exactly", {
  df <- read_text("a,a,\n1,2,3\n", header = NA)
  expect_identical(names(df), c("a", "a", ""))
})

test_that("single-record files are decided the same way", {
  names_only <- read_text("a,b\n", header = NA)
  expect_identical(names(names_only), c("a", "b"))
  expect_identical(nrow(names_only), 0L)

  data_only <- read_text("1,2\n", header = NA)
  expect_identical(names(data_only), c("V1", "V2"))
  expect_identical(nrow(data_only), 1L)
})

test_that("a file with no record is still a 0x0 data frame", {
  # There is no first record to judge, and pass 2 is never reached; the
  # unresolved sentinel must not survive as a truthy header flag.
  expect_identical(dim(read_text("", header = NA)), c(0L, 0L))
  expect_identical(dim(read_text("\n\n\n", header = NA)), c(0L, 0L))
})

test_that("detection sees the first record after a BOM is stripped", {
  expect_identical(names(read_text(c(BOM, charToRaw("a,b\n1,2\n")), header = NA)),
                   c("a", "b"))
  expect_identical(names(read_text(c(BOM, charToRaw("1,2\n3,4\n")), header = NA)),
                   c("V1", "V2"))
})

test_that("a leading record of empty cells is judged, not skipped", {
  # zsv drops leading all-zero-length records unless keep_empty_header_rows
  # is set; without it detection would judge record 2 (SS10).
  empty_names <- read_text(",\n1,2\n", header = NA)
  expect_identical(names(empty_names), c("", ""))
  expect_identical(nrow(empty_names), 1L)

  quoted_empty <- read_text("\"\",\"\"\nx,y\n", header = NA)
  expect_identical(names(quoted_empty), c("", ""))
  expect_identical(nrow(quoted_empty), 1L)
})

test_that("a detected header reports bad bytes the way an explicit one does", {
  # Detection routes record 1 into the header branch, which has no names
  # yet, so the message locates the cell but cannot name the column. The
  # verdict must not change which message you get.
  bad_header <- c(charToRaw("a,"), BAD_UTF8, charToRaw("\n1,2\n"))
  expect_error(read_text(bad_header, header = NA),
               "invalid UTF-8 at row 1, column 2$")
  expect_error(read_text(bad_header, header = TRUE),
               "invalid UTF-8 at row 1, column 2$")

  # detected as data instead, the generated name is available and used
  expect_error(read_text(c(charToRaw("1,"), BAD_UTF8, charToRaw("\n3,4\n")),
                         header = NA),
               "row 1, column 2 \\(\"V2\"\\)")
  # and a later record names the detected header's own column
  expect_error(read_text(c(charToRaw("a,b\n1,"), BAD_UTF8, charToRaw("\n")),
                         header = NA),
               "row 2, column 2 \\(\"b\"\\)")
})

test_that("detection judges the first real record after blank ones", {
  header_verdict <- read_text("\n\na,b\n1,2\n", header = NA)
  expect_identical(names(header_verdict), c("a", "b"))
  expect_identical(nrow(header_verdict), 1L)

  # the data verdict in the same position: a regression that judged the
  # blank record instead of the first real one would pass without this
  data_verdict <- read_text("\n\n1,2\n3,4\n", header = NA)
  expect_identical(names(data_verdict), c("V1", "V2"))
  expect_identical(nrow(data_verdict), 2L)
})

test_that("header = NA is the only thing that changes", {
  txt <- "1,2\n3,4\n"
  expect_identical(read_text(txt, header = NA), read_text(txt, header = FALSE))
  expect_identical(nrow(read_text(txt, header = TRUE)), 1L)
})

test_that("the C entry point re-validates header independently", {
  # The wrapper's own messages are test-errors.R's job; what is only
  # checkable here is that native code does not trust them (design SS7).
  path <- csv_file("a,b\n1,2\n")
  expect_error(.Call(zucsv:::C_read_csv, path, c(TRUE, TRUE), ",", "NA", NULL),
               "TRUE, FALSE or NA")
  expect_error(.Call(zucsv:::C_read_csv, path, "auto", ",", "NA", NULL),
               "TRUE, FALSE or NA")
  # NA reaches C as the detect sentinel rather than being rejected
  expect_identical(names(.Call(zucsv:::C_read_csv, path, NA, ",", "NA", NULL)),
                   c("a", "b"))
})
