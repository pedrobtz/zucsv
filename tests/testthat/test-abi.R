# The shared object and the LinkingTo surface (#23, design SS30): what
# zucsv exports, and what libzucsv.a holds, calls and leaves mutable. The
# helpers are zuarrow's, in helper-abi.R.

test_that("the shared object exports R_init_zucsv only", {
  skip_on_cran()
  abi_skip_instrumented()
  exports <- abi_exports()
  expect_identical(setdiff(exports, "R_init_zucsv"), character())
  expect_true("R_init_zucsv" %in% exports)
})

test_that("no symbol CRAN's compiled-code check reports is referenced", {
  skip_on_cran()
  abi_skip_instrumented()
  imports <- sub("^_+", "", abi_imports())
  # The object calls R's API, so an empty or unparsed listing is a broken
  # tool or pattern, not a clean object.
  expect_true("Rf_allocVector" %in% imports)
  runtime <- sub("^_+", "", abi_runtime_imports())
  expect_identical(
    intersect(setdiff(imports, runtime), abi_forbidden),
    character()
  )
})

test_that("the archive and its headers are installed", {
  skip_on_cran()
  expect_true(file.exists(abi_archive_path()))
  # Loaded from source (devtools::test(), as gctorture runs it), there is
  # no installed include directory to check; R CMD check installs one.
  testthat::skip_if_not(
    dir.exists(system.file("Meta", package = "zucsv")),
    "zucsv is loaded from source, not installed"
  )
  include <- system.file("include", package = "zucsv")
  for (f in c("zucsv.h", "zucsv-r.h")) {
    expect_true(file.exists(file.path(include, f)), info = f)
  }
})

test_that("the header and the archive agree on the ABI", {
  expect_identical(.Call(C_abi_version), c(1L, 1L))
})

test_that("the archive calls no R and nothing CRAN's check reports", {
  skip_on_cran()
  abi_skip_instrumented()
  undefined <- sub("^_+", "", abi_archive_undefined())
  # It does call the C library, so an empty listing is a broken tool.
  expect_true("malloc" %in% undefined)
  expect_identical(
    grep("^(Rf_|R_|Rprintf|REprintf)", undefined, value = TRUE),
    character()
  )
  expect_identical(intersect(undefined, abi_forbidden), character())
})

test_that("the archive holds zucsv and zsv, and none of zuarrow", {
  skip_on_cran()
  abi_skip_instrumented()
  defined <- abi_archive_defined_globals()
  expect_true("zucsv_open_stream" %in% defined)
  expect_true("zsv_new" %in% defined)
  # zuarrow's builders are called, never carried: a consumer links
  # libzuarrow.a once, after libzucsv.a.
  expect_identical(grep("zuarrow|^Arrow", defined, value = TRUE), character())
  expect_true(any(grepl("^zuarrowArrow", abi_archive_undefined())))
  # collate_header_destroy is zsv's one global without its prefix
  # (design SS30).
  expect_identical(
    grep("^(zucsv_|zsv_)", defined, value = TRUE, invert = TRUE),
    "collate_header_destroy"
  )
})

test_that("the archive holds no mutable global", {
  skip_on_cran()
  abi_skip_instrumented()
  expect_identical(abi_archive_writable(), character())
})
