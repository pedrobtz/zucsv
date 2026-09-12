# Type inference (design SS11). Each rule has at least one fixture, including
# the spellings that are deliberately NOT recognised.

test_that("logical is inferred from exactly four spellings", {
  expect_identical(read_text("a\nTRUE\nFALSE\n")$a, c(TRUE, FALSE))
  expect_identical(read_text("a\ntrue\nfalse\n")$a, c(TRUE, FALSE))
  expect_identical(read_text("a\nTRUE\ntrue\nFALSE\nfalse\n")$a,
                   c(TRUE, TRUE, FALSE, FALSE))
})

test_that("other boolean spellings are character, not logical", {
  expect_type(read_text("a\nT\nF\n")$a, "character")
  expect_type(read_text("a\nTrue\nFalse\n")$a, "character")
  expect_type(read_text("a\nyes\nno\n")$a, "character")
  expect_type(read_text("a\nTRue\n")$a, "character")
  # 0 and 1 are integers, which is the point of excluding them from logical
  expect_type(read_text("a\n0\n1\n")$a, "integer")
})

test_that("integer syntax is base 10 with an optional sign", {
  expect_identical(read_text("a\n0\n42\n-17\n")$a, c(0L, 42L, -17L))
  expect_identical(read_text("a\n+9\n")$a, 9L)
  # leading zeros are integers, as in utils::type.convert()
  expect_identical(read_text("a\n007\n42\n")$a, c(7L, 42L))
})

test_that("integers outside R's range widen the column to double", {
  expect_type(read_text("a\n2147483648\n1\n")$a, "double")
  expect_identical(read_text("a\n2147483647\n1\n")$a, c(2147483647L, 1L))
  # INT_MIN is NA_integer_ in R, so it is not an available integer value
  expect_type(read_text("a\n-2147483648\n1\n")$a, "double")
  expect_identical(read_text("a\n-2147483647\n")$a, -2147483647L)
})

test_that("double syntax covers decimals, exponents, Inf and NaN", {
  expect_identical(read_text("a\n1.5\n-0.25\n")$a, c(1.5, -0.25))
  expect_identical(read_text("a\n6.02e23\n1E-8\n")$a, c(6.02e23, 1e-8))
  expect_identical(read_text("a\n.5\n5.\n")$a, c(0.5, 5))
  expect_identical(read_text("a\nInf\n-Inf\n+Inf\n")$a, c(Inf, -Inf, Inf))
  expect_identical(read_text("a\nNaN\n1\n")$a, c(NaN, 1))
})

test_that("near-miss numeric spellings fall back to character", {
  for (txt in c("a\ninf\n", "a\nInfinity\n", "a\nnan\n", "a\n0x1A\n",
                "a\n1e\n", "a\n1e+\n", "a\n.\n", "a\n1.2.3\n",
                "a\n+\n", "a\n-\n", "a\n1..2\n",
                # a decimal comma, quoted so it is not read as a delimiter
                "a\n\"1,5\"\n")) {
    expect_identical(typeof(read_text(txt)$a), "character", info = txt)
  }
})

test_that("whitespace is never trimmed, so a padded number is character", {
  expect_type(read_text("a\n 42\n1\n")$a, "character")
  expect_identical(read_text("a\n 42\n1\n")$a, c(" 42", "1"))
  expect_type(read_text("a\n42 \n1\n")$a, "character")
})

test_that("a mixed column widens rather than dropping values", {
  expect_type(read_text("a\n1\n2.5\n")$a, "double")
  expect_identical(read_text("a\n1\n2.5\n")$a, c(1, 2.5))
  # logical, integer and double are independent tests; a column that fails
  # all three is character, so TRUE next to 1 is not "integer"
  expect_type(read_text("a\nTRUE\n1\n")$a, "character")
  expect_type(read_text("a\n1\nx\n")$a, "character")
})

test_that("missing values do not affect the inferred type", {
  expect_type(read_text("a\n1\nNA\n2\n")$a, "integer")
  expect_identical(read_text("a\n1\nNA\n2\n")$a, c(1L, NA, 2L))
  expect_type(read_text("a\nTRUE\n\nFALSE\n")$a, "logical")
  expect_type(read_text("a\n1.5\nNA\n")$a, "double")
})

test_that("a column with no non-missing values is character", {
  expect_type(read_text("a\nNA\nNA\n")$a, "character")
  expect_identical(read_text("a\nNA\nNA\n")$a, c(NA_character_, NA_character_))
  expect_type(read_text("a,b\n,1\n,2\n")$a, "character")
})

test_that("each column is inferred independently", {
  df <- read_text("i,d,s,l\n1,1.5,x,TRUE\n2,2.5,y,FALSE\n")
  expect_identical(vapply(df, typeof, ""),
                   c(i = "integer", d = "double", s = "character", l = "logical"))
})

test_that("inference does not depend on the order rows arrive in", {
  expect_identical(read_text("a\n1\n2.5\n")$a, read_text("a\n2.5\n1\n")$a[c(2, 1)])
  expect_type(read_text("a\nx\n1\n")$a, "character")
  expect_type(read_text("a\n1\nx\n")$a, "character")
})

test_that("numeric values match base R exactly", {
  # conversion goes through R_strtod, the same routine as as.numeric()
  for (v in c("1.5", "0.1", "1e308", "1e-308", "3.141592653589793",
              "6.02214076e23", "-0.0")) {
    expect_identical(read_text(paste0("a\n", v, "\n"))$a, as.numeric(v),
                     info = v)
  }
})


test_that("double conversion is bit-identical to as.numeric() on every branch", {
  # zucsv transcribes R's own R_strtod5 rather than calling it (convert.c),
  # so this pins that the transcription stays faithful: overflow, underflow,
  # denormals, huge mantissas, capped exponents, the sign of zero.
  vals <- c("0", "-0", "+0", "0e999", "0E-999", "0000.5000", "007",
            "1e308", "1.7976931348623157e308", "1.7976931348623158e308", "1e309", "-1e309",
            "2.2250738585072014e-308", "2.2250738585072011e-308", "4.9e-324", "2e-324", "1e-400",
            "123456789012345678901234567890", "1234567890123456789012345678901234567890.5",
            "0.1", "0.799012", "2.235263", "3.141592653589793", "1e22", "1e23",
            "9007199254740993", "9007199254740992.5", "18446744073709551616",
            "1e-307", "1e-308", "1.5e-310", "123e-310", ".5", "5.", "1.e5", ".5e-3",
            "1e9999", "1e-9999", "1e99999999999", "28690487184014.2158750030608700e323")
  got <- read_text(paste0("a\n", paste(vals, collapse = "\n"), "\n"))$a
  want <- as.numeric(vals)
  expect_type(got, "double")
  expect_identical(got, want)
  # identical() treats 0 and -0 as equal; the bytes must match too
  expect_identical(lapply(got, writeBin, con = raw()), lapply(want, writeBin, con = raw()))
})
