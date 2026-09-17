# Reading CSV files with zucsv

``` r

library(zucsv)
```

`zucsv` reads delimited text files into ordinary R data frames, using
the [`zsv`](https://github.com/liquidaty/zsv) C parser as its backend.
This article covers the whole package — there are two exported functions
— and, more usefully, the handful of decisions that make it behave
differently from the readers you already have.

## Why another CSV package

R is not short of CSV readers. `zucsv` is not trying to replace
`data.table` or `readr`; it is a smaller thing with different
priorities:

- **Semantics you can check.** Numeric conversion is bit-identical to
  [`as.numeric()`](https://rdrr.io/r/base/numeric.html), by construction
  rather than by coincidence.
- **Errors where the problem is.** Invalid UTF-8, an embedded NUL or a
  ragged row is an error naming the record and column, not a value that
  fails later somewhere else.
- **No silent repair.** Duplicate column names stay duplicated, nothing
  is whitespace-trimmed, and a value that will not convert to a type you
  asked for is an error rather than `NA`.
- **No dependencies, no system library.** The parser is bundled and
  pinned.
- **A small surface.** One function to read a file and one to report how
  it would be read, five arguments between them, and a documented list
  of what `zucsv` deliberately does not do.

If you want the fastest possible reader, use
[`data.table::fread()`](https://rdrr.io/pkg/data.table/man/fread.html).
If you want tidyverse integration, use `readr`. `zucsv` optimises for
predictability.

## The API

``` r

read_csv(file, header = TRUE, delimiter = ",", na = c("", "NA"), col_types = NULL)
```

| argument | what it does |
|----|----|
| `file` | path to a local file; `~` is expanded |
| `header` | `TRUE` first record supplies names, `FALSE` generate `V1`…`Vn`, `NA` detect which |
| `delimiter` | any single ASCII byte — comma, tab, semicolon, pipe |
| `na` | exact cell values to read as missing, or `NULL` for none |
| `col_types` | force `logical`, `integer`, `double` or `character` |

`header = NA` is for files whose shape you do not know in advance. It
reads the first record as names unless one of its unquoted fields is a
number or a logical — so `id,name,score` is a header and `1,Ada,9.5` is
data. It judges that one record and nothing else, which is cheap and
predictable but less informed than the equivalent in
[`data.table::fread()`](https://rdrr.io/pkg/data.table/man/fread.html)
or DuckDB;
[`?read_csv`](https://pedrobtz.github.io/zucsv/reference/read_csv.md)
lists the three shapes where the verdicts differ. When you know the
answer, say so.

``` r

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
#> 'data.frame':    3 obs. of  4 variables:
#>  $ id    : int  1 2 3
#>  $ name  : chr  "Ada" "Linus" "Grace"
#>  $ score : num  9.5 8 NA
#>  $ active: logi  TRUE FALSE TRUE
```

If you want to see those decisions before committing to them,
[`sniff_csv()`](https://pedrobtz.github.io/zucsv/reference/sniff_csv.md)
reports them without building any columns — and its output feeds
straight back in, which turns an inferred read into a strict one:

``` r

s <- sniff_csv(path)
str(s)
#> List of 5
#>  $ header   : logi TRUE
#>  $ ncol     : int 4
#>  $ nrow     : int 3
#>  $ col_names: chr [1:4] "id" "name" "score" "active"
#>  $ col_types: chr [1:4] "integer" "character" "double" "logical"

# same data frame, but nothing is inferred this time
identical(read_csv(path, header = s$header, col_types = s$col_types),
          read_csv(path))
#> [1] TRUE
```

That second form is worth reaching for in a script that runs unattended:
pin the schema once, and a file that changes shape becomes an error
instead of a differently-typed column.

Other delimiters need no separate function:

``` r

tsv <- tempfile(fileext = ".tsv")
writeLines(c("a\tb", "1\t2"), tsv)
read_csv(tsv, delimiter = "\t")
#>   a b
#> 1 1 2
```

## Type inference

Columns are inferred over `logical`, `integer`, `double` and
`character`, in that order, using **every** value in the column rather
than a sample. A column is only narrower than `character` if every
non-missing value in the whole file fits.

``` r

types <- tempfile(fileext = ".csv")
writeLines(c("lgl,int,dbl,chr", "TRUE,1,1.5,x", "FALSE,2,2.5,y"), types)
vapply(read_csv(types), typeof, "")
#>         lgl         int         dbl         chr 
#>   "logical"   "integer"    "double" "character"
```

Only four spellings are logical — `TRUE`, `FALSE`, `true`, `false`. `T`,
`yes` and `1` are not, because in real data they are far more often
something else.

``` r

spellings <- tempfile(fileext = ".csv")
writeLines(c("a,b,c", "T,yes,TRUE", "F,no,FALSE"), spellings)
vapply(read_csv(spellings), typeof, "")
#>           a           b           c 
#> "character" "character"   "logical"
```

### Forcing types

`col_types` takes one value for every column, or one value for all of
them. A cell that will not convert is an error naming the row, the
column and the value:

``` r

read_csv(path, col_types = c("integer", "character", "double", "logical"))
#>   id  name score active
#> 1  1   Ada   9.5   TRUE
#> 2  2 Linus   8.0  FALSE
#> 3  3 Grace    NA   TRUE

read_csv(path, col_types = "integer")
#> Error in `read_csv()`:
#> ! Cannot parse row 2, column 2 ("name") as integer: "Ada"
```

That strictness is the point. A reader that turned `"Ada"` into `NA`
here would be losing data quietly.

## Four things that surprise people

These are all deliberate, and all of them are the kind of thing worth
knowing before you meet them in a 200-megabyte file.

**Blank lines are skipped, everywhere.** Leading, trailing or in the
middle — they are not rows.

``` r

blanks <- tempfile(fileext = ".csv")
writeLines(c("a,b", "1,2", "", "3,4", ""), blanks)
nrow(read_csv(blanks))
#> [1] 2
```

A line of only whitespace or only delimiters is *not* blank: it is a
record of empty cells, and must match the table’s width. In a one-column
file, write an intentional empty cell as `""` — a bare empty line is
skipped.

**Nothing is trimmed.** `" 42"` is text, because it has a space in it.

``` r

padded <- tempfile(fileext = ".csv")
writeLines(c("a", " 42", "1"), padded)
read_csv(padded)$a
#> [1] " 42" "1"
```

**Leading zeros are integers.** This matches
[`utils::type.convert()`](https://rdrr.io/r/utils/type.convert.html),
and it is how postal codes and zip codes lose their leading zero in
every reader. Use `col_types` when the column is an identifier rather
than a number.

``` r

codes <- tempfile(fileext = ".csv")
writeLines(c("zip", "0600000", "1000001"), codes)
read_csv(codes)$zip
#> [1]  600000 1000001
read_csv(codes, col_types = "character")$zip
#> [1] "0600000" "1000001"
```

**Column names are kept exactly**, duplicates and empty strings
included. No name repair.

``` r

dupes <- tempfile(fileext = ".csv")
writeLines(c("a,a,", "1,2,3"), dupes)
names(read_csv(dupes))
#> [1] "a" "a" ""
```

## Errors say where

Structural problems name the record and the column. Records are numbered
as they appear, header included, so the numbers match what you see in an
editor.

``` r

ragged <- tempfile(fileext = ".csv")
writeLines(c("a,b", "1,2", "3,4,5"), ragged)
read_csv(ragged)
#> Error in `read_csv()`:
#> ! CSV row 3 has 3 fields; expected 2
```

Text that cannot be carried into an R string is an error too, rather
than something that fails later:

``` r

bad <- tempfile(fileext = ".csv")
con <- file(bad, "wb")
writeBin(c(charToRaw("a,b\n"), as.raw(c(0xFF, 0xFE)), charToRaw(",2\n")), con)
close(con)
read_csv(bad)
#> Error in `read_csv()`:
#> ! CSV contains invalid UTF-8 at row 2, column 1 ("a")
```

The [KEN_ALL
article](https://pedrobtz.github.io/zucsv/articles/japanese-postal-codes.md)
shows why that matters on a real file, and what the other readers do
with the same input.

## Missing values

`na` is a set of exact cell values, matched after unquoting. Quoted and
unquoted forms are therefore the same thing, and nothing is trimmed
first.

``` r

miss <- tempfile(fileext = ".csv")
writeLines(c("a,b,c", 'NA,"NA", NA'), miss)
read_csv(miss)
#>      a    b   c
#> 1 <NA> <NA>  NA

read_csv(miss, na = NULL)
#>    a  b   c
#> 1 NA NA  NA
```

## Performance

Faster than
[`utils::read.csv()`](https://rdrr.io/r/utils/read.table.html) by 2–12×
depending on shape; slower than
[`data.table::fread()`](https://rdrr.io/pkg/data.table/man/fread.html),
which is multi-threaded and single-pass. The [benchmark
results](https://github.com/pedrobtz/zucsv/blob/main/bench/RESULTS.md)
have the numbers and the method, including why the comparison against
`vroom` needs care.

## What it does not do

v0.1 reads local files and nothing else. There is no `write_csv()`, no
URL or connection input, no compressed files, no date parsing, no column
selection, no `skip` or `n_max`, and no `encoding` argument — convert
the file first, as the KEN_ALL article shows. The full list, and the
reasoning, is in the package’s design document.
