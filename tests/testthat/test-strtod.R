# The double converter (design decision 5, #36). zucsv carries a
# transcription of R_strtod5()'s decimal branch, built once with a long
# double and once with a double accumulator, each with two shortcuts.
# Calibration at load picks the instance that agrees with R_strtod, or falls
# back to R_strtod itself. C_strtod_mode() (tests only) forces each mode, so
# every one is held to its oracle here:
#   * the mode in use, and the R_strtod fallback, against as.numeric();
#   * each fast instance against its verbatim twin, which has R's loops
#     with no shortcuts: the shortcuts must not change a bit.

strtod_inputs <- function() {
  had_seed <- exists(".Random.seed", envir = globalenv(), inherits = FALSE)
  if (had_seed) old_seed <- get(".Random.seed", envir = globalenv())
  on.exit(
    if (had_seed) assign(".Random.seed", old_seed, envir = globalenv())
    else rm(".Random.seed", envir = globalenv())
  )
  set.seed(20261119)
  n <- 4000
  mantissa <- vapply(sample(1:40, n, replace = TRUE), function(k) {
    paste(sample(0:9, k, replace = TRUE), collapse = "")
  }, "")
  point <- vapply(mantissa, function(m) {
    at <- sample(0:nchar(m), 1)
    if (at == 0 || at == nchar(m)) m else paste0(substr(m, 1, at), ".", substring(m, at + 1))
  }, "", USE.NAMES = FALSE)
  exponent <- ifelse(runif(n) < 0.5, "", paste0("e", sample(-340:340, n, replace = TRUE)))
  sign <- sample(c("", "-", "+"), n, replace = TRUE)
  c(
    # each branch: shortcuts' edges, overflow, underflow, denormals, -0
    "0", "-0", "0.0", "1", "0.1", "0.799012", "2.235263", "-0.685363",
    "3.141592653589793", "9007199254740992", "9007199254740993",
    "18446744073709551615", "18446744073709551616", "1e22", "1e23", "1e27",
    "1e28", "1.7976931348623157e308", "1.8e308", "4.9e-324", "2.4e-324",
    "1e-307", "1e-308", "123e-310", "1.5e-310", "0e999",
    paste0("1", strrep("0", 300)), paste0("0.", strrep("1", 300)),
    paste0(sign, point, exponent)
  )
}

read_as_double <- function(x) {
  read_csv(text = c("x", x), col_types = "double", na = NULL)$x
}

with_strtod_mode <- function(mode, code) {
  old <- .Call(zucsv:::C_strtod_mode, as.integer(mode))
  on.exit(.Call(zucsv:::C_strtod_mode, old))
  code
}

same_bits <- function(a, b) identical(a, b, num.eq = FALSE)

test_that("the calibrated converter agrees with as.numeric() bit for bit", {
  x <- strtod_inputs()
  expect_true(same_bits(read_as_double(x), as.numeric(x)))
  expect_true(.Call(zucsv:::C_strtod_mode, NULL) %in% 0:2)
})

test_that("the R_strtod fallback agrees with as.numeric(), long cells included", {
  # Cells over 255 bytes take the malloc path in zucsv_strtod_call().
  x <- c(strtod_inputs(), paste0("1.", strrep("3", 400), "e-5"))
  got <- with_strtod_mode(2L, read_as_double(x))
  expect_true(same_bits(got, as.numeric(x)))
})

test_that("each fast instance agrees with R's verbatim loops (#36)", {
  x <- strtod_inputs()
  ld <- with_strtod_mode(1L, read_as_double(x))
  ld_verbatim <- with_strtod_mode(3L, read_as_double(x))
  d <- with_strtod_mode(0L, read_as_double(x))
  d_verbatim <- with_strtod_mode(4L, read_as_double(x))
  expect_true(same_bits(ld, ld_verbatim))
  expect_true(same_bits(d, d_verbatim))
  # and whichever accumulator matches this R is the one in use
  expect_true(same_bits(ld, as.numeric(x)) || same_bits(d, as.numeric(x)))
})

test_that("the mode hook rejects anything but a mode", {
  expect_error(.Call(zucsv:::C_strtod_mode, 5L), "from 0 to 4", fixed = TRUE)
  expect_error(.Call(zucsv:::C_strtod_mode, "1"), "from 0 to 4", fixed = TRUE)
})
