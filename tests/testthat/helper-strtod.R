# Helpers for test-strtod.R, kept out of the test file so the suite runs
# shuffled (#29).

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
