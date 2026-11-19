test_that("a row-width mismatch names the record and both widths", {
  expect_error(read_text("a,b\n1,2,3\n"), "CSV row 2 has 3 fields; expected 2", fixed = TRUE)
  expect_error(read_text("a,b\n1\n"), "CSV row 2 has 1 field; expected 2", fixed = TRUE)
  # records are numbered as the parser emits them, header included, and
  # skipped blank records still consume a number
  expect_error(read_text("a,b\n1,2\n\n1,2,3\n"), "CSV row 4 has 3 fields", fixed = TRUE)
})

test_that("record numbering counts a quoted newline as one record", {
  expect_error(read_text("a,b\n\"x\ny\",2\n1,2,3\n"), "CSV row 3 has 3 fields", fixed = TRUE)
})

test_that("a missing or unreadable file is reported with its path", {
  missing <- file.path(tempdir(), "zucsv-does-not-exist.csv")
  expect_error(read_csv(missing), "Cannot open CSV file", fixed = TRUE)
  # fopen() succeeds on a directory on most Unixes, so this must be caught
  # rather than read as an empty file
  expect_error(read_csv(tempdir()), "is a directory, not a CSV file", fixed = TRUE)
})

test_that("argument shapes are rejected before the file is touched", {
  expect_error(read_csv(c("a", "b")), "single non-missing file path")
  expect_error(read_csv(NA_character_), "single non-missing file path")
  expect_error(read_csv(1), "single non-missing file path")

  f <- csv_file("a,b\n1,2\n")
  # NA is the detect sentinel, not an error; see test-header-detection.R
  expect_error(read_csv(f, header = c(TRUE, FALSE)), "must be TRUE, FALSE or NA")
  expect_error(read_csv(f, header = "yes"), "must be TRUE, FALSE or NA")
  expect_error(read_csv(f, header = 1), "must be TRUE, FALSE or NA")
  expect_error(read_csv(f, header = NULL), "must be TRUE, FALSE or NA")
  expect_error(read_csv(f, na = 1), "character vector or NULL")
  expect_error(read_csv(f, na = c("x", NA)), "may not contain NA")
})

test_that("the delimiter must be one ASCII byte and not a structural one", {
  f <- csv_file("a,b\n1,2\n")
  expect_error(read_csv(f, delimiter = ""), "single ASCII byte")
  expect_error(read_csv(f, delimiter = "ab"), "single ASCII byte")
  expect_error(read_csv(f, delimiter = "\u00e9"), "single ASCII byte")
  for (d in c("\n", "\r", "\f", "\"")) {
    expect_error(read_csv(f, delimiter = d), "may not be a newline")
  }
})

test_that("col_types is validated for names, and for length once known", {
  f <- csv_file("a,b\n1,2\n")
  expect_error(read_csv(f, col_types = "numeric"), "Unknown: numeric")
  expect_error(read_csv(f, col_types = c("integer", "nope")), "Unknown: nope")
  expect_error(read_csv(f, col_types = character(0)), "character vector of type names")
  expect_error(read_csv(f, col_types = 1), "character vector of type names")
  expect_error(
    read_csv(f, col_types = rep("character", 3)),
    "col_types has length 3 but the CSV has 2 columns",
    fixed = TRUE
  )
})

test_that("the column cap is 65536, straddled on both routes (#33)", {
  skip_under_gctorture() # 65,536 names; under 0.1 s anywhere else
  header <- function(n) paste0(paste0("c", seq_len(n), collapse = ","), "\n")
  for (n in c(65535L, 65536L)) {
    expect_identical(ncol(read_text(header(n))), n)
    expect_identical(ncol(read_csv(text = header(n))), n)
    expect_identical(sniff_csv(text = header(n))$ncol, n)
  }
  msg <- "exceeds zucsv's maximum of 65536 columns"
  expect_error(read_text(header(65537L)), msg, fixed = TRUE)
  expect_error(read_csv(text = header(65537L)), msg, fixed = TRUE)
  expect_error(sniff_csv(text = header(65537L)), msg, fixed = TRUE)
  # a data row over the cap, under a header at it
  over <- paste0(header(65536L), paste(rep("1", 65537L), collapse = ","), "\n")
  expect_error(read_text(over), msg, fixed = TRUE)
})

test_that("reading leaves no file handles behind, on success or failure", {
  skip_on_cran()
  skip_on_os(c("windows", "solaris"))
  if (Sys.which("lsof") == "") skip("lsof not available")

  count_open <- function() {
    out <- suppressWarnings(system2("lsof", c("-p", Sys.getpid()), stdout = TRUE, stderr = FALSE))
    sum(grepl("zucsvleak", out, fixed = TRUE))
  }
  dir <- file.path(tempdir(), "zucsvleak"); dir.create(dir, showWarnings = FALSE)
  on.exit(unlink(dir, recursive = TRUE), add = TRUE)
  good <- file.path(dir, "good.csv"); writeLines(c("a,b", "1,2"), good)
  bad <- file.path(dir, "bad.csv"); writeLines(c("a,b", "1,2,3"), bad)

  before <- count_open()
  for (i in 1:50) invisible(read_csv(good))
  for (i in 1:50) try(read_csv(bad), silent = TRUE)
  expect_identical(count_open(), before)
})

test_that("every error path releases what it holds, on both routes (#38)", {
  # Ungated so the sanitizer and valgrind legs run it: a leak on any
  # Rf_error() path shows there as a definite loss with a stack. One input
  # per error the reader raises, each read a few times through a file and
  # through text.
  bad <- list(
    width = "a,b\n1,2,3\n",
    unclosed = "a,b\n1,\"x\n2,3\n",
    oversize = paste0("a\n1\n2,", strrep("X", 8388700), "\n3\n"),
    nul = c(charToRaw("a\n1\n"), NUL, charToRaw("\n")),
    utf8 = c(charToRaw("a\n"), BAD_UTF8, charToRaw("\n")),
    forced = "a\nx\n"
  )
  for (nm in names(bad)) {
    input <- bad[[nm]]
    path <- csv_file(input)
    for (i in 1:3) {
      expect_error(read_csv(path, col_types = if (nm == "forced") "integer"), info = nm)
      # sniff_csv() takes no col_types, so a forced type is not an error there
      if (nm != "forced") expect_error(sniff_csv(path), info = nm)
    }
    if (is.character(input)) {
      expect_error(read_csv(text = input, col_types = if (nm == "forced") "integer"), info = nm)
    }
    unlink(path)
  }
  # and repeated successful reads
  good <- csv_file("a,b\n1,2\n3,4\n")
  for (i in 1:50) expect_identical(nrow(read_csv(good)), 2L)
})
