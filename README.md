
# zucsv

<!-- badges: start -->
[![R-CMD-check](https://github.com/pedrobtz/zucsv/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/pedrobtz/zucsv/actions/workflows/R-CMD-check.yaml)
[![native-checks](https://github.com/pedrobtz/zucsv/actions/workflows/native-checks.yaml/badge.svg)](https://github.com/pedrobtz/zucsv/actions/workflows/native-checks.yaml)
[![Coverage](https://github.com/pedrobtz/zucsv/raw/main/.github/badges/coverage.svg)](https://github.com/pedrobtz/zucsv/actions/workflows/coverage.yaml)
[![Lifecycle: experimental](https://img.shields.io/badge/lifecycle-experimental-orange.svg)](https://lifecycle.r-lib.org/articles/stages.html#experimental)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](https://opensource.org/license/mit)
<!-- CRAN status: uncomment on acceptance
[![CRAN status](https://www.r-pkg.org/badges/version/zucsv)](https://CRAN.R-project.org/package=zucsv)
-->
<!-- badges: end -->

Read CSV files into base R data frames, using the
[`zsv`](https://github.com/liquidaty/zsv) C parser as the backend.

`zucsv` is deliberately small: one exported function, no R package
dependencies, and a bundled copy of the parser so there is nothing to install
and nothing to configure. The division of labour is the whole design —
`zsv` decides where rows and fields begin and end, `zucsv` decides what they
mean in R.

## Installation

``` r
# install.packages("pak")
pak::pak("pedrobtz/zucsv")
```

## Example

``` r
library(zucsv)

path <- tempfile(fileext = ".csv")
writeLines(c(
  "id,name,score,active",
  "1,Ada,9.5,TRUE",
  "2,Linus,8.0,FALSE",
  "3,Grace,,TRUE"
), path)

df <- read_csv(path)
df
#>   id  name score active
#> 1  1   Ada   9.5   TRUE
#> 2  2 Linus   8.0  FALSE
#> 3  3 Grace    NA   TRUE

str(df)
#> 'data.frame':	3 obs. of  4 variables:
#>  $ id    : int  1 2 3
#>  $ name  : chr  "Ada" "Linus" "Grace"
#>  $ score : num  9.5 8 NA
#>  $ active: logi  TRUE FALSE TRUE
```

Types are inferred over `logical`, `integer`, `double` and `character`. Give
`col_types` to fix them instead — a value that will not convert is an error
rather than a silent `NA`:

``` r
read_csv(path, col_types = "character")
read_csv(path, col_types = c("integer", "character", "double", "logical"))

read_csv(path, col_types = "integer")
#> Error: Cannot parse row 2, column 2 ("name") as integer: "Ada"
```

Other delimiters need no separate function:

``` r
read_csv("data.tsv", delimiter = "\t")
read_csv("data.txt", delimiter = ";")
```

## What it does not do

v0.1 reads local files and nothing else. There is no `write_csv()`, no URL or
connection input, no compressed files, no date parsing, no column selection,
no `skip`/`n_max`. See section 3 of [`zucsv-design.md`](https://github.com/pedrobtz/zucsv/blob/main/.agents/zucsv-design.md) for
the full list and the reasoning, and section 27 for what is likely to come
next.

## Behavior worth knowing

These are decisions, not accidents, and each is pinned by a test:

* **Blank lines are skipped** wherever they appear. A line of only whitespace
  or only delimiters is *not* blank — it is a record of empty cells, and must
  match the table's width. In a single-column file write an intentional empty
  cell as `""`; a bare empty line is skipped.
* **Every record must have the same width.** Short rows are not padded.
* **Column names are kept exactly**, duplicates and empty strings included.
  No name repair.
* **Text must be valid UTF-8.** Invalid bytes or an embedded NUL are an error
  where they occur, rather than something that fails later somewhere less
  helpful. A BOM is ignored.
* **Nothing is trimmed**, so `" 42"` is character, not a number.

## Performance

Faster than `utils::read.csv()` by 2.5–6.8× across shapes; slower than
`data.table::fread()`, which is multi-threaded and single-pass; comparable to
a fully materialised `vroom`, ahead of it on character-heavy data, wide
tables and small files. Numbers and method in
[`bench/RESULTS.md`](bench/RESULTS.md).

## Licence

MIT. The bundled `zsv` sources are MIT, copyright Guarnerix Inc dba
Liquidaty; see `inst/COPYRIGHTS` and `src/vendor/zsv/UPSTREAM`.
