# Read a CSV file

Reads a delimited text file into a base R data frame, using the bundled
`zsv` C parser. The file is read twice: once to establish the shape and
validate it, once to fill columns that were allocated at their exact
final length.

## Usage

``` r
read_csv(
  file,
  header = TRUE,
  delimiter = ",",
  na = c("", "NA"),
  col_types = NULL
)
```

## Arguments

- file:

  Path to a local file, as a single string. `~` is expanded.

- header:

  If `TRUE` (the default), the first record supplies the column names.
  If `FALSE`, every record is data and columns are named `V1`, `V2`, ...
  Header values are always text: they are never matched against `na` and
  never type-inferred.

- delimiter:

  The field delimiter, as a single ASCII character. Defaults to `","`. A
  newline, carriage return, form feed or double quote is not allowed.

- na:

  Character vector of cell values to read as missing, or `NULL` to
  disable missing-value matching. Matching happens after unquoting and
  is exact: no whitespace is trimmed, so `NA` and `"NA"` are both
  missing under the default but `" NA"` is not.

- col_types:

  `NULL` to infer each column's type, or a character vector of
  `"logical"`, `"integer"`, `"double"` or `"character"`. A single value
  applies to every column; otherwise give one per column. A cell that
  cannot be converted to a forced type is an error, not a missing value.

## Value

A [data.frame](https://rdrr.io/r/base/data.frame.html) with one column
per field. With `header = FALSE`, or for an empty file, see the notes
above for how names and dimensions are determined. An empty file gives a
data frame with zero rows and zero columns; a header-only file gives
zero rows but one column per header field.

## Details

Some behavior worth knowing before you rely on it:

- **Blank lines are skipped**, wherever they appear, and do not count as
  rows. A line holding only whitespace or only delimiters is *not*
  blank: it is a record of empty cells and must match the table's width.
  In a single-column file, write an intentional empty cell as `""` — a
  bare empty line is skipped.

- **Every record must have the same width.** A mismatch is an error;
  short rows are not padded.

- **Column names are kept exactly as they appear**, including duplicates
  and empty strings. No name repair is performed.

- **Text must be valid UTF-8.** A byte sequence that is not, or an
  embedded NUL, is an error rather than something passed through to fail
  later. A UTF-8 BOM is ignored.

- A CR LF pair inside a quoted field is normalised to a single LF.

## Examples

``` r
path <- tempfile(fileext = ".csv")
write.csv(
  data.frame(id = 1:3, name = c("Ada", "Linus", "Grace")),
  path,
  row.names = FALSE
)

read_csv(path)
#>   id  name
#> 1  1   Ada
#> 2  2 Linus
#> 3  3 Grace

# Force a column's type rather than inferring it
read_csv(path, col_types = c("character", "character"))
#>   id  name
#> 1  1   Ada
#> 2  2 Linus
#> 3  3 Grace

unlink(path)
```
