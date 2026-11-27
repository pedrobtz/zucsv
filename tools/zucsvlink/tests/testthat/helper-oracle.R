# zucsv is the oracle where it is installed: tools/ci/consumer.sh installs
# it and sets ZUCSVLINK_ORACLE, so there a missing zucsv fails; the run with
# zucsv and zuarrow moved away skips these.
skip_without_zucsv <- function() {
  if (!requireNamespace("zucsv", quietly = TRUE)) {
    if (nzchar(Sys.getenv("ZUCSVLINK_ORACLE"))) {
      stop("ZUCSVLINK_ORACLE is set but zucsv is not installed")
    }
    testthat::skip("zucsv is not installed")
  }
}

extdata <- function(name) system.file("extdata", name, package = "zucsvlink")

csv_file <- function(x) {
  path <- tempfile(fileext = ".csv")
  writeBin(if (is.raw(x)) x else charToRaw(x), path)
  path
}

# read_stream()'s columns as read_csv() lays out a data frame.
as_df <- function(res) {
  n <- if (length(res$columns)) length(res$columns[[1]]) else 0L
  structure(
    res$columns,
    row.names = if (n == 0L) integer() else c(NA_integer_, -n),
    class = "data.frame"
  )
}

same_bits <- function(a, b) identical(a, b, num.eq = FALSE, single.NA = FALSE)

# Decimals whose conversion depends on the accumulator R used: the probe
# must have chosen the instance that agrees with as.numeric().
decimals <- function() {
  set.seed(20261127)
  m <- vapply(sample(1:20, 2000, TRUE), function(k) {
    paste(sample(0:9, k, TRUE), collapse = "")
  }, "")
  c(
    "0.799012", "2.235263", "-0.685363", "1e-308", "4.9e-324", "1e23",
    "1.7976931348623157e308", "NaN", "Inf", "-Inf",
    paste0(m, "e", sample(-320:300, 2000, TRUE))
  )
}
