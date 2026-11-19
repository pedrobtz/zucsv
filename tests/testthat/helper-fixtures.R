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

# Heavy tests check limits, not PROTECT correctness, so they add nothing
# under gctorture while costing it hours: under gctorture2(step = 20) a
# 65,536-column table or 200,000 rows is a GC every 20 allocations for the
# whole run (it timed the job out at 60 minutes). r-actions turns gctorture on
# in-process with gctorture2(), so it can be detected rather than opted out
# of. gctorture2(0) returns the current step; calling it again restores it.
under_gctorture <- function() {
  step <- gctorture2(0)
  gctorture2(step)
  step > 0
}

skip_under_gctorture <- function() {
  if (under_gctorture()) testthat::skip("heavy under gctorture")
}

# Stress tests: every CI leg runs them (2-3 s in all, #33). They skip on CRAN,
# for memory (a 12 MB cell, 200,000 rows), and under gctorture.
# ZUCSV_STRESS=true (or 1, yes) forces them on anywhere but gctorture:
#
#   ZUCSV_STRESS=true Rscript -e 'devtools::test()'
skip_unless_stress <- function() {
  skip_under_gctorture()
  forced <- tolower(Sys.getenv("ZUCSV_STRESS")) %in% c("true", "1", "yes")
  if (!forced) testthat::skip_on_cran()
}
