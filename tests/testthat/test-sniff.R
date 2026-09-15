# sniff_csv() runs read_csv()'s pass 1 and stops. The invariant worth most
# here is that the two cannot disagree, so most of this file checks the
# report against what read_csv() actually does on the same input.

sniff_text <- function(x, ...) sniff_csv(csv_file(x), ...)

# every shape that exercises a different branch of the pass
FIXTURES <- c(
  header        = "id,name,score\n1,Ada,9.5\n2,Grace,8\n",
  headerless    = "1,2,3\n4,5,6\n",
  all_character = "x,y,z\nx,y,z\n",
  quoted_names  = "\"id\",\"2024\"\n1,5\n",
  empty_names   = ",\n1,2\n",
  header_only   = "a,b\n",
  one_record    = "1,2\n",
  blanks        = "\n\na,b\n1,2\n\n",
  logicals      = "a,b\nTRUE,FALSE\nfalse,true\n",
  widening      = "a\n1\n2.5\n",
  na_cells      = "a,b\n1,NA\n2,\n",
  empty_file    = ""
)

test_that("the report has the documented shape", {
  s <- sniff_text(FIXTURES[["header"]])
  expect_type(s, "list")
  expect_identical(names(s),
                   c("header", "ncol", "nrow", "col_names", "col_types"))
  expect_identical(s$header, TRUE)
  expect_identical(s$ncol, 3L)
  expect_identical(s$nrow, 2L)
  expect_identical(s$col_names, c("id", "name", "score"))
  expect_identical(s$col_types, c("integer", "character", "double"))
  # the scalars are scalars, not length-one lists
  expect_length(s$header, 1L)
  expect_type(s$ncol, "integer")
  expect_type(s$nrow, "integer")
})

test_that("the report matches what read_csv actually does", {
  for (nm in names(FIXTURES)) {
    txt <- FIXTURES[[nm]]
    path <- csv_file(txt)
    s <- sniff_csv(path)
    df <- read_csv(path, header = NA)

    expect_identical(s$ncol, ncol(df), info = nm)
    expect_identical(s$nrow, nrow(df), info = nm)
    expect_identical(s$col_names, names(df), info = nm)
    expect_identical(s$col_types,
                     unname(vapply(df, typeof, "")), info = nm)
  }
})

test_that("sniffing then reading strictly gives the same data frame", {
  # This is the documented workflow: pin the decisions, then read with
  # nothing inferred. It must not change the answer.
  for (nm in names(FIXTURES)) {
    path <- csv_file(FIXTURES[[nm]])
    s <- sniff_csv(path)
    pinned <- if (s$ncol == 0L) {
      read_csv(path, header = s$header)
    } else {
      read_csv(path, header = s$header, col_types = s$col_types)
    }
    expect_identical(pinned, read_csv(path, header = NA), info = nm)
  }
})

test_that("the header verdict is the first-record rule, not a separate one", {
  # If sniff_csv grew its own detection these would drift apart.
  expect_true(sniff_text("a,b\n1,2\n")$header)
  expect_false(sniff_text("1,2\n3,4\n")$header)
  expect_true(sniff_text("\"2024\",\"x\"\n1,2\n")$header)   # decision 21
  expect_false(sniff_text("id,2024\n1,5\n")$header)         # documented divergence
  expect_true(sniff_text("NA,NA\n1,2\n")$header)            # documented divergence
})

test_that("nrow is exact, not an estimate", {
  many <- paste0("a,b\n", paste(sprintf("%d,%d", 1:500, 1:500), collapse = "\n"), "\n")
  expect_identical(sniff_text(many)$nrow, 500L)
  # blank records are not rows, here as anywhere else
  expect_identical(sniff_text("a,b\n1,2\n\n\n3,4\n")$nrow, 2L)
})

test_that("na and delimiter shape the inferred types", {
  # with "" missing, the column is integer; with na = NULL it is character
  expect_identical(sniff_text("a\n1\n\"\"\n")$col_types, "integer")
  expect_identical(sniff_text("a\n1\n\"\"\n", na = NULL)$col_types, "character")
  expect_identical(sniff_text("a\tb\n1\t2\n", delimiter = "\t")$col_names,
                   c("a", "b"))
})

test_that("sniffing applies read_csv's validation", {
  expect_error(sniff_text("a,b\n1,2,3\n"), "CSV row 2 has 3 fields")
  expect_error(sniff_text(c(charToRaw("a,b\n1,"), BAD_UTF8, charToRaw("\n"))),
               "invalid UTF-8")
  expect_error(sniff_text(c(charToRaw("a,b\n1,"), NUL, charToRaw("\n"))),
               "NUL")
  expect_error(sniff_csv(tempfile()), "Cannot open CSV file")
})

test_that("an empty file reports an empty answer rather than failing", {
  s <- sniff_text("")
  expect_identical(s$header, FALSE)
  expect_identical(s$ncol, 0L)
  expect_identical(s$nrow, 0L)
  expect_identical(s$col_names, character(0))
  expect_identical(s$col_types, character(0))
  expect_identical(sniff_text("\n\n\n"), s)
})

test_that("argument shapes are rejected before the file is touched", {
  expect_error(sniff_csv(c("a", "b")), "single non-missing file path")
  expect_error(sniff_csv(NA_character_), "single non-missing file path")
  f <- csv_file("a,b\n1,2\n")
  expect_error(sniff_csv(f, delimiter = "ab"), "single ASCII byte")
  expect_error(sniff_csv(f, delimiter = "\n"), "may not be a newline")
  expect_error(sniff_csv(f, na = 1), "character vector or NULL")
  expect_error(sniff_csv(f, na = c("x", NA)), "may not contain NA")
  expect_error(sniff_csv(dirname(f)), "is a directory")
})

test_that("the C entry point re-validates independently of the wrapper", {
  f <- csv_file("a,b\n1,2\n")
  expect_error(.Call(zucsv:::C_sniff_csv, f, "ab", "NA"), "single ASCII byte")
  expect_error(.Call(zucsv:::C_sniff_csv, c(f, f), ",", "NA"),
               "single non-missing file path")
  expect_error(.Call(zucsv:::C_sniff_csv, f, ",", 1), "character vector or NULL")
})
