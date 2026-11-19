# The double converter (design decision 5, #36). zucsv carries a
# transcription of R_strtod5()'s decimal branch, built once with a long
# double and once with a double accumulator, each with two shortcuts.
# Calibration at load picks the instance that agrees with R_strtod, or falls
# back to R_strtod itself. C_strtod_mode() (tests only) forces each mode, so
# every one is held to its oracle here:
#   * the mode in use, and the R_strtod fallback, against as.numeric();
#   * each fast instance against its verbatim twin, which has R's loops
#     with no shortcuts: the shortcuts must not change a bit.

test_that("the calibrated converter agrees with as.numeric() bit for bit", {
  x <- strtod_inputs()
  expect_true(same_bits(read_as_double(x), as.numeric(x)))
  # 3 is the verbatim long double instance, chosen where long double has
  # fewer bits at run time than LDBL_MANT_DIG claims (valgrind)
  expect_true(.Call(zucsv:::C_strtod_mode, NULL) %in% 0:3)
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
  # The long double shortcuts assume LDBL_MANT_DIG bits. Where long double
  # has fewer at run time, calibration picks the verbatim instance (mode 3)
  # instead, and the fast one is not expected to agree.
  if (.Call(zucsv:::C_strtod_mode, NULL) != 3L) {
    expect_true(same_bits(ld, ld_verbatim))
  }
  expect_true(same_bits(d, d_verbatim))
  # and whichever instance this R matches is the one in use
  expect_true(same_bits(ld, as.numeric(x)) || same_bits(ld_verbatim, as.numeric(x)) ||
                same_bits(d, as.numeric(x)))
})

test_that("the mode hook rejects anything but a mode", {
  expect_error(.Call(zucsv:::C_strtod_mode, 5L), "from 0 to 4", fixed = TRUE)
  expect_error(.Call(zucsv:::C_strtod_mode, "1"), "from 0 to 4", fixed = TRUE)
})
