# Report how a CSV file would be read

Runs the first of
[`read_csv()`](https://pedrobtz.github.io/zucsv/reference/read_csv.md)'s
two passes and returns what it learned — the header verdict, the shape,
the column names and the inferred column types — without building any
columns. Use it when you want to see the decisions before committing to
them, or to pin them down so a later run cannot drift:

## Usage

``` r
sniff_csv(file = NULL, delimiter = ",", na = c("", "NA"), text = NULL)
```

## Arguments

- file:

  Path to a local file, as a single string. `~` is expanded. Supply this
  or `text`, not both.

- delimiter:

  The field delimiter, as a single ASCII character. Defaults to `","`.
  Not detected — `zucsv` never guesses the delimiter.

- na:

  Character vector of cell values to read as missing, or `NULL` to
  disable missing-value matching. Affects the inferred types, so pass
  here whatever you will pass to
  [`read_csv()`](https://pedrobtz.github.io/zucsv/reference/read_csv.md).

- text:

  The CSV itself, as a character vector, instead of a path, with the
  same meaning as in
  [`read_csv()`](https://pedrobtz.github.io/zucsv/reference/read_csv.md).

## Value

A list with five elements:

- `header`:

  `TRUE` if the first record is names, `FALSE` if it is data, by the
  rule documented under `header` in
  [`read_csv()`](https://pedrobtz.github.io/zucsv/reference/read_csv.md).

- `ncol`:

  Number of columns.

- `nrow`:

  Number of data rows — exact, not an estimate, because the pass counts
  every record.

- `col_names`:

  The names the file would produce: the first record when `header` is
  `TRUE`, otherwise `V1`, `V2`, ...

- `col_types`:

  Inferred type of each column, suitable for passing straight back as
  `col_types`.

An empty file gives `header = FALSE`, zero columns and zero rows.

## Details

    s <- sniff_csv(path)
    read_csv(path, header = s$header, col_types = s$col_types)

That second call reads the file strictly: nothing about it is inferred,
so it gives the same answer next month even if the data changes shape.

Sniffing reads the whole file and applies exactly the validation
[`read_csv()`](https://pedrobtz.github.io/zucsv/reference/read_csv.md)
applies, so a ragged row, an embedded NUL or invalid UTF-8 is an error
here too. If `sniff_csv()` succeeds,
[`read_csv()`](https://pedrobtz.github.io/zucsv/reference/read_csv.md)
on the same file will not fail on content.

It costs one pass rather than two, but that pass is the expensive one:
do not call it before every read as a matter of course. Its purpose is
to let you *stop* inferring, not to infer twice.

## See also

[`read_csv()`](https://pedrobtz.github.io/zucsv/reference/read_csv.md)

## Examples

``` r
path <- tempfile(fileext = ".csv")
writeLines(c("id,name,score", "1,Ada,9.5", "2,Grace,8"), path)

sniff_csv(path)
#> $header
#> [1] TRUE
#> 
#> $ncol
#> [1] 3
#> 
#> $nrow
#> [1] 2
#> 
#> $col_names
#> [1] "id"    "name"  "score"
#> 
#> $col_types
#> [1] "integer"   "character" "double"   
#> 

# Pin the decisions, then read without inferring anything
s <- sniff_csv(path)
read_csv(path, header = s$header, col_types = s$col_types)
#>   id  name score
#> 1  1   Ada   9.5
#> 2  2 Grace   8.0

unlink(path)
```
