# The vector scan (#31). zsv skips whole vectors that hold no token, and once
# placed the token it found relative to where the skip began, so it rescanned
# the run before every token: correct values, quadratic cost.

test_that("long runs between tokens read correctly at every vector width (#31)", {
  # Runs straddling 16, 32 and 64 bytes (the SSE2, AVX2 and NEON widths) and
  # a few thousand, quoted and unquoted, with escapes, delimiters and line
  # breaks inside quotes. utils::read.csv() is the oracle. A quoted CR LF is
  # left out: read.csv() drops the CR and zucsv keeps it (#24), which the
  # structure tests pin.
  runs <- c(1:3, 14:18, 30:34, 62:66, 1000, 4097)
  cells <- character()
  for (n in runs) {
    x <- strrep("x", n)
    cells <- c(
      cells,
      x,
      paste0("\"", x, "\""),
      paste0("\"", x, "\"\"", x, "\""),
      paste0("\"", x, ",", x, "\""),
      paste0("\"", x, "\n", x, "\"")
    )
  }
  for (term in c("\n", "\r\n")) {
    rows <- paste(cells, rev(cells), sep = ",")
    txt <- paste0("a,b", term, paste(rows, collapse = term), term)
    want <- utils::read.csv(text = txt, colClasses = "character")
    got <- read_text(txt, na = NULL)
    expect_identical(got$a, want$a, info = encodeString(term))
    expect_identical(got$b, want$b, info = encodeString(term))
    expect_identical(read_csv(text = txt, na = NULL), got)
  }
})

test_that("scan cost does not grow with the distance between tokens (#31)", {
  skip_on_cran() # a timing ratio
  # The same 3.2 MB as 3200 rows of 1 KiB cells and as 50 rows of 64 KiB
  # cells. Before the fix the second took about 40 times as long; a linear
  # scan takes about the same time for both.
  make <- function(width) {
    row <- c(charToRaw("1,\""), as.raw(rep.int(0x58L, width)), charToRaw("\"\n"))
    csv_file(c(charToRaw("id,txt\n"), rep(row, 3276800L / width)))
  }
  short <- make(1024L)
  long <- make(65536L)
  on.exit(unlink(c(short, long)), add = TRUE)
  time <- function(path) {
    min(vapply(1:3, function(i) system.time(sniff_csv(path))[["elapsed"]], 0))
  }
  # Floor the denominator so a very fast machine does not divide by zero.
  expect_lt(time(long) / max(time(short), 0.005), 8)
})
