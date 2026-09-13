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

# Stress tests are gated on an explicit opt-in rather than skip_on_cran().
#
# They check limits -- the column cap, the record-size cap, millions of rows --
# not PROTECT correctness, so they add nothing to the gctorture job while
# costing it hours: testthat::test_local() sets NOT_CRAN, and under
# gctorture2(step = 20) a 65,536-column table or 200,000 rows is a GC every
# 20 allocations for the whole run. That timed the job out at 60 minutes.
#
#   ZUCSV_STRESS=true Rscript -e 'devtools::test()'
skip_unless_stress <- function() {
  if (!identical(tolower(Sys.getenv("ZUCSV_STRESS")), "true")) {
    testthat::skip("set ZUCSV_STRESS=true to run stress tests")
  }
}
