test_that("the header and the archive agree on the ABI", {
  expect_identical(abi(), c(1L, 1L))
})

test_that("the probe at load chose a mode", {
  expect_true(strtod_mode() %in% 0:4)
})

test_that("the stream reads the fixture on a thread of its own", {
  for (threaded in c(TRUE, FALSE)) {
    res <- read_stream(extdata("types.csv"), threaded = threaded)
    expect_identical(res$code, 0L)
    expect_identical(res$formats, c("b", "i", "g", "u"))
    expect_identical(
      res$columns,
      list(
        l = c(TRUE, NA, FALSE),
        i = c(1L, NA, -2147483647L),
        d = c(1.5, -Inf, 0.1),
        s = c("a", "x,y", "\u00e9")
      )
    )
  }
})

test_that("errors come back as a code and read_csv()'s message", {
  res <- read_stream(csv_file("a,b\n1,2,3\n"))
  expect_identical(res$code, 22L)
  expect_identical(res$message, "CSV row 2 has 3 fields; expected 2")
  missing <- file.path(tempdir(), "zucsvlink-no-such.csv")
  res <- read_stream(missing)
  expect_identical(res$code, 5L)
  expect_identical(res$message, paste("Cannot open CSV file:", missing))
})

test_that("its values equal read_csv()'s, doubles bit for bit", {
  skip_without_zucsv()
  x <- decimals()
  files <- list(
    types = list(extdata("types.csv")),
    decimals = list(csv_file(paste0("x\n", paste(x, collapse = "\n"), "\n")),
                    col_types = "double", na = NULL),
    detect = list(csv_file("1,TRUE\n2,FALSE\n"), header = NA),
    semicolon = list(csv_file("a;b\nx;1\n;2\n"), delimiter = ";")
  )
  for (nm in names(files)) {
    args <- files[[nm]]
    want <- do.call(zucsv::read_csv, args)
    for (b in c(65536, 7)) {
      got <- as_df(do.call(read_stream, c(args, batch_rows = b)))
      expect_true(same_bits(got, want), info = paste(nm, b))
    }
  }
})

test_that("the probe agrees with zucsv's own choice", {
  skip_without_zucsv()
  expect_identical(strtod_mode(), .Call(zucsv:::C_strtod_mode, NULL))
})
