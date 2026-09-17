# Reading Japan Post's KEN_ALL, legacy and modern

Japan Post publishes every postal code in the country as a CSV, and has
done for decades. It comes in two editions: an original in Shift-JIS,
and a newer one in UTF-8. Between them they exercise most of what makes
real CSV files harder than the examples in a parser’s test suite, so
they make a good place to show what `zucsv` does and — just as usefully
— where it deliberately disagrees with the other readers.

## The legacy file is not UTF-8, and that matters

`KEN_ALL.CSV` is Shift-JIS, has no header, and uses CRLF. Ask `zucsv` to
read it and you get an error rather than a data frame:

``` r

library(zucsv)
read_csv(sjis, header = FALSE)
#> Error in `read_csv()`:
#> ! CSV contains invalid UTF-8 at row 1, column 4 ("V4")
```

That is `zucsv` doing what it is supposed to do. Row 1, column 4 is the
first field in the file containing a byte that is not valid UTF-8 — the
half-width katakana `ﾎｯｶｲﾄﾞｳ`, at byte offset 26. The message names the
record and the column, so you know both *what* is wrong and *where*.

It is worth comparing, because the alternatives are not obviously worse
until you look closely.

``` r

first_bytes <- function(x) paste(utils::head(charToRaw(x), 6), collapse = " ")

# data.table and vroom both accept the file and return something
dt <- data.table::fread(sjis, header = FALSE, showProgress = FALSE)
vr <- vroom::vroom(sjis, delim = ",", col_names = FALSE,
                   show_col_types = FALSE, altrep = FALSE)

c(fread = first_bytes(dt[[7]][1]),
  vroom = first_bytes(vr[[7]][1]),
  truth = first_bytes(
    utils::read.csv(sjis, header = FALSE, fileEncoding = "CP932")[1, 7]))
#>               fread               vroom               truth 
#> "96 6b 8a 43 93 b9" "96 6b 8a 43 93 b9" "e5 8c 97 e6 b5 b7"
```

Column 7 of the first record is the prefecture name, 北海道. Read
correctly it is nine bytes of UTF-8. `fread` and `vroom` hand back the
raw Shift-JIS bytes with no encoding mark, so nothing has failed yet —
the failure arrives later, somewhere else, in whatever code first tries
to treat those bytes as text.

[`utils::read.csv()`](https://rdrr.io/r/utils/read.table.html) does
error, but it takes about 100× longer to do it, and the message points
at a byte sequence rather than a location. It is also an error you
cannot print safely: the message itself contains the invalid bytes.

### Reading it properly

Convert the encoding first, then read:

``` r

converted <- file.path(dir, "KEN_ALL-utf8.csv")
writeLines(
  iconv(readLines(sjis, encoding = "CP932", warn = FALSE), "CP932", "UTF-8"),
  converted, useBytes = TRUE
)

old <- read_csv(converted, header = FALSE)
dim(old)
#> [1] 124812     15
old[1, c(3, 7, 8, 9)]
#>       V3     V7           V8                   V9
#> 1 600000 北海道 札幌市中央区 以下に掲載がない場合
```

## The modern file

`utf_ken_all.csv` is the same data, normalised to UTF-8 and to one row
per record:

``` r

new <- read_csv(utf8, header = FALSE)
dim(new)
#> [1] 124498     15
vapply(new, function(x) class(x)[1], "")
#>          V1          V2          V3          V4          V5          V6 
#>   "integer" "character"   "integer" "character" "character" "character" 
#>          V7          V8          V9         V10         V11         V12 
#> "character" "character" "character"   "integer"   "integer"   "integer" 
#>         V13         V14         V15 
#>   "integer"   "integer"   "integer"
```

Every call so far has passed `header = FALSE`, because this file has no
header and `zucsv` will otherwise take its first record for one. You do
not have to know that in advance — `header = NA` works it out:

``` r

detected <- read_csv(utf8, header = NA)
fr <- data.table::fread(utf8, showProgress = FALSE)

rbind(zucsv = c(rows = nrow(detected), first_name = names(detected)[1]),
      fread = c(rows = nrow(fr),       first_name = names(fr)[1]))
#>       rows     first_name
#> zucsv "124498" "V1"      
#> fread "124498" "V1"
```

Record 1 begins `01101`, which is integer syntax, and one
numeric-looking field is enough to settle that the record is data.
`fread` reaches the same verdict by a different route: it compares the
first record against the types inferred from the rest of the file, while
`zucsv` looks only at the record itself. Cheaper, and on this file
identical — but the two rules are not the same rule, and
[`?read_csv`](https://pedrobtz.github.io/zucsv/reference/read_csv.md)
lists the three shapes where they part company. Passing `header`
explicitly is still better when you know the answer.

Kanji comes back marked as UTF-8, with the character counts you would
expect:

``` r

x <- new[[7]][1]
c(value = x, encoding = Encoding(x), nchar = nchar(x), bytes = nchar(x, "bytes"))
#>    value encoding    nchar    bytes 
#> "北海道"  "UTF-8"      "3"      "9"
```

## The postal-code trap

Column 3 is the seven-digit postal code, and it is the reason this file
is a good teacher. Hokkaido’s first entry is `0600000`:

``` r

as_text <- read_csv(utf8, header = FALSE, col_types = "character")
as_text[[3]][1]
#> [1] "0600000"

# inferred, it is a number
new[[3]][1]
#> [1] 600000

# how many codes would lose a leading zero
mean(substr(as_text[[3]], 1, 1) == "0")
#> [1] 0.1199859
```

About one code in eight begins with a zero, and reading the column as a
number destroys it. `zucsv` infers `integer` here, and so do
[`read.csv()`](https://rdrr.io/r/utils/read.table.html) and `fread` —
this is not a difference between readers, it is what “looks like a
number” means. The fix is to say so:

``` r

fixed <- read_csv(utf8, header = FALSE,
                  col_types = c("integer", rep("character", 8),
                                rep("integer", 6)))
fixed[[3]][1]
#> [1] "0600000"
```

Leading zeros being integer syntax is a deliberate choice in `zucsv`,
matching
[`utils::type.convert()`](https://rdrr.io/r/utils/type.convert.html).
`col_types` is the documented escape, and this file is exactly the case
it exists for.

## Whitespace is never trimmed

Column 2 is the old five-digit code, space-padded to five characters:

``` r

as_text[[2]][1]
#> [1] "060  "
class(new[[2]])
#> [1] "character"
class(utils::read.csv(utf8, header = FALSE)[[2]])
#> [1] "numeric"
```

`zucsv` leaves `"060 "` as text, because it is text — nothing is trimmed
before deciding a type. Base R trims first and calls it the number 60.
Both are defensible; `zucsv` prefers not to silently discard bytes that
were in the file.

## One more thing the two editions differ on

The legacy file splits long town names across consecutive records that
share a postal code; the modern one keeps each record whole.

``` r

c(old_rows = nrow(old), new_rows = nrow(new))
#> old_rows new_rows 
#>   124812   124498

# the longest town field in the modern file
i <- which.max(nchar(as_text[[9]]))
nchar(as_text[[9]][i])
#> [1] 297
```

That is a property of the data, not of CSV syntax — every reader sees
the same row counts, and reassembling split records is the caller’s job
either way.

## What to take from this

- **Encoding problems should be loud.** `zucsv` refuses invalid UTF-8
  and says where it is. Deferring the failure to whatever code touches
  the string next is not a kindness.
- **`col_types` is not a performance knob.** On this file it is the
  difference between keeping postal codes and quietly corrupting one in
  eight of them.
- **Type inference cannot read your mind.** “Looks like a number” is a
  syntactic question, and every reader here answers it the same way.
