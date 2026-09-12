# Fixtures are written as raw bytes so line endings, BOMs, NULs and invalid
# UTF-8 are exact, and so the test files stay pure ASCII.

csv_file <- function(x) {
  path <- tempfile(fileext = ".csv")
  con <- file(path, "wb")
  on.exit(close(con))
  writeBin(if (is.raw(x)) x else charToRaw(x), con)
  path
}

read_text <- function(x, ...) read_csv(csv_file(x), ...)

BOM <- as.raw(c(0xEF, 0xBB, 0xBF))
BAD_UTF8 <- as.raw(c(0xFF, 0xFE))
NUL <- as.raw(0x00)
