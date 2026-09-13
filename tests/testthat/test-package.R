test_that("the package's shared object is loaded", {
  expect_true("zucsv" %in% names(getLoadedDLLs()))
})
