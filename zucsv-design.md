# zucsv — Design for v0.1.0

**Status:** Draft  
**Backend:** vendored `zsv` / `libzsv`  
**Primary API:** `read_csv()`  
**Scope:** small, read-only first release

## 1. Purpose

`zucsv` is a small R package for reading CSV files using the `zsv` C
parser as its native backend. The first release should prove the
integration, establish stable R semantics, and remain easy to audit and
maintain.

The package should follow the same general philosophy as the other `zu`
packages: use a focused, high-quality C engine for low-level parsing
while keeping R-facing behavior small and explicit.

The package name already provides the namespace, so exported functions
should use conventional names rather than repeating the `zu` prefix:

``` r

zucsv::read_csv("data.csv")
```

not:

``` r

zucsv::zu_read_csv("data.csv")
```

## 2. Goals for v0.1

The first release should:

- read a local CSV file into a base R `data.frame`;
- use vendored `zsv` for CSV tokenization and row parsing;
- correctly handle quoted fields, embedded delimiters, embedded
  newlines, escaped quotes, CRLF/LF input, empty fields, and UTF-8 text;
- support files with or without a header row;
- support a configurable single-byte delimiter;
- support missing-value strings;
- infer the basic R column types `logical`, `integer`, `double`, and
  `character`;
- allow callers to force column types when inference is undesirable;
- provide deterministic errors for malformed table structure or failed
  type conversion;
- build on normal R toolchains on Linux, macOS, and Windows;
- have no mandatory R package dependencies.

The implementation should favor correctness, simple ownership rules, and
testability over exposing every feature available in `zsv`.

## 3. Non-goals for v0.1

The first release will intentionally not provide:

- `write_csv()`;
- `read_tsv()` as a separate exported function;
- URL input;
- R connections;
- compressed-file handling;
- automatic decompression;
- date, time, or datetime inference;
- locale-aware numeric parsing;
- factors;
- column selection;
- row skipping or `n_max`;
- progress bars;
- multi-row headers;
- ragged-row recovery;
- parallel parsing;
- an exposed SIMD/fast-parser switch;
- ALTREP-backed columns;
- lazy parsing;
- schema metadata beyond ordinary R column types.

These features can be considered only after the basic parser-to-R
boundary is stable.

## 4. Public API

The initial public API consists of one exported function:

``` r

read_csv(
  file,
  header = TRUE,
  delimiter = ",",
  na = c("", "NA"),
  col_types = NULL
)
```

### `file`

A length-one character path to a local file.

For v0.1, paths are the only supported input source. Restricting input
to seekable local files makes the implementation substantially simpler
and permits an exact two-pass allocation strategy.

The file is opened in binary mode by the native implementation.

### `header`

A length-one logical value.

- `TRUE`: the first parsed row supplies column names.
- `FALSE`: all rows are data and names are generated as `V1`, `V2`, …
  `Vn`.

Header values are always treated as text and are never subject to `na`
matching or type inference.

### `delimiter`

A length-one, single-byte character string. The default is `","`.

This allows semicolon-, tab-, and pipe-delimited files without adding
more exported functions in v0.1:

``` r

read_csv("data.tsv", delimiter = "\t")
read_csv("data.txt", delimiter = ";")
```

Newline, carriage return, form feed, NUL, and quote are rejected as
delimiters.

### `na`

A character vector of exact cell values that should become missing
values. The default is:

``` r

c("", "NA")
```

Matching occurs after CSV unquoting. Therefore quoted and unquoted forms
are treated identically:

``` text
NA
"NA"
```

both become missing values under the default configuration.

`na = NULL` disables missing-value matching.

No whitespace trimming is performed before matching in v0.1.

### `col_types`

Controls column conversion.

`NULL` means infer column types from the complete file.

A character vector may force types. Supported values are:

``` text
logical
integer
double
character
```

A length-one value applies to every column:

``` r

read_csv("codes.csv", col_types = "character")
```

A vector with one value per column specifies each column explicitly:

``` r

read_csv(
  "data.csv",
  col_types = c("integer", "character", "double")
)
```

No shorthand type codes and no named partial specifications are
supported in v0.1.

## 5. Return value

`read_csv()` returns an ordinary base R `data.frame`.

The implementation constructs the data frame directly as a named list of
equal-length vectors and assigns the standard `data.frame` class and
compact row names. It should not route through
[`data.frame()`](https://rdrr.io/r/base/data.frame.html) and should not
perform factor conversion.

Column names from a header are preserved exactly in v0.1, including
duplicates and empty names. No automatic name repair is performed. This
avoids silently changing information from the source file and keeps name
policy outside the CSV parser.

Examples:

``` csv
id,name,score
1,Ada,9.5
2,Linus,8.0
```

becomes conceptually:

``` r

data.frame(
  id = c(1L, 2L),
  name = c("Ada", "Linus"),
  score = c(9.5, 8.0),
  check.names = FALSE
)
```

## 6. Why `zsv`

`zsv` is used as the parsing engine rather than implementing CSV syntax
in the R package.

The relevant characteristics for `zucsv` are:

- a C library API intended for embedding;
- streaming parsing;
- direct access to parsed cells as pointer/length pairs;
- configurable delimiter handling;
- explicit handling of real-world quoting and newline cases;
- low memory overhead;
- a pull API that maps naturally to an R row-processing loop;
- the possibility of using newer fast/SIMD parsing modes in a later
  release without changing the R-level API.

Only the library code required for parsing is vendored. The `zsv`
command-line application and unrelated facilities such as SQL, SQLite,
JSON conversion, interactive viewing, and other CLI commands are not
part of `zucsv`.

## 7. Native interface

The package uses the base R C API and `.Call` directly. It should not
depend on Rcpp or cpp11.

The exported R function validates simple arguments and calls one
registered C routine, conceptually:

``` c
SEXP C_read_csv(
    SEXP file,
    SEXP header,
    SEXP delimiter,
    SEXP na,
    SEXP col_types
);
```

Native symbols must be registered in `src/init.c`, and dynamic symbol
lookup should be disabled.

The R wrapper remains intentionally thin:

``` r

read_csv <- function(file,
                     header = TRUE,
                     delimiter = ",",
                     na = c("", "NA"),
                     col_types = NULL) {
  .Call(C_read_csv, file, header, delimiter, na, col_types)
}
```

Argument validation that affects native safety should still be repeated
or enforced in C.

## 8. Parser API choice

`zucsv` should use `zsv`’s pull parsing API for v0.1:

``` c
while (zsv_next_row(parser) == zsv_status_row) {
    size_t n = zsv_cell_count(parser);

    for (size_t i = 0; i < n; ++i) {
        struct zsv_cell cell = zsv_get_cell(parser, i);
        /* consume cell.str / cell.len */
    }
}
```

The pull API is preferred because it gives the R wrapper an ordinary
sequential control flow, makes error handling easier, and avoids
invoking R-oriented logic from parser callbacks.

The push API can be benchmarked later, but it is not needed to establish
the package architecture.

## 9. Two-pass reading strategy

v0.1 should use two complete parsing passes over the file.

This is a deliberate simplification enabled by accepting only local file
paths.

### Pass 1: inspect

The first pass:

1.  reads the header, if present;
2.  determines the expected number of columns;
3.  counts data rows;
4.  verifies that every data row has the expected width;
5.  applies `na` matching;
6.  infers column types when `col_types = NULL`;
7.  validates forced values when useful to do so early;
8.  checks size limits before allocating R vectors.

No full R data frame is constructed during this pass.

### Pass 2: materialize

The second pass:

1.  allocates each R column at its exact final length;
2.  parses the file again;
3.  converts each cell directly into the destination R vector;
4.  constructs character strings only for character columns;
5.  assigns names, row names, and the `data.frame` class.

The resulting data path is:

``` text
file
  ↓
zsv parser
  ↓
cell { pointer, length }
  ↓
direct conversion
  ↓
preallocated R column
```

This avoids growable R columns, repeated vector copying, temporary row
lists, and complicated type widening during materialization.

The cost is parsing the file twice. For the initial implementation, the
simpler allocation and ownership model is worth that tradeoff.
Benchmarks should later determine whether a one-pass column builder is
worthwhile.

## 10. Column-width semantics

The expected number of columns is established by:

- the header row when `header = TRUE`;
- the first data row when `header = FALSE`.

Every subsequent row must contain exactly that number of cells.

A mismatch is an error:

``` text
CSV row 42 has 6 fields; expected 5
```

Although `zsv` itself can parse tables with inconsistent row widths,
`zucsv` v0.1 deliberately requires a rectangular table because every R
data-frame column must have the same length.

Future versions may add an explicit ragged-row policy such as padding
short rows with `NA`, but v0.1 must not silently invent that policy.

## 11. Type inference

When `col_types = NULL`, inference uses all non-missing values in each
column during pass 1.

For each column, track whether every observed non-missing value can be
represented as:

- logical;
- integer;
- double.

The final type is selected in this order:

``` text
all logical  → logical
all integer  → integer
all numeric  → double
otherwise    → character
```

A column containing only missing values becomes `character` in v0.1
because there is no evidence for a narrower type.

### Logical syntax

Recognize only:

``` text
TRUE
FALSE
true
false
```

Values `0`, `1`, `T`, `F`, `yes`, and `no` are not logical syntax in
v0.1.

### Integer syntax

Integers use base-10 syntax with an optional leading sign and no
grouping separator:

``` text
0
42
-17
+9
```

A value must fit in R’s integer range to keep the column as integer. A
syntactically integral value outside that range promotes the column to
double if it is exactly acceptable under the numeric parser.

### Double syntax

Basic decimal and scientific notation are supported using `.` as the
decimal separator:

``` text
1.5
-0.25
6.02e23
1E-8
```

Locale-specific decimal commas and thousands separators are not
supported in v0.1.

### Character fallback

Any non-missing value that does not satisfy the selected primitive types
makes the inferred column `character`.

Type inference must not mutate the original text representation before
the final type is known.

## 12. Forced type conversion

When `col_types` forces a type, every non-missing cell must be valid for
that type.

Conversion failure is an error, not a warning followed by `NA`:

``` text
Cannot parse row 18, column 3 ("amount") as double: "12 USD"
```

This strict behavior prevents silent data loss and keeps v0.1 semantics
easy to reason about.

`character` always accepts a non-NUL cell.

## 13. Text and encoding

v0.1 treats text input as UTF-8.

A UTF-8 BOM at the beginning of the file should be ignored before
interpreting the first field or header name.

No encoding argument or transcoding is provided in v0.1. Supporting
Latin-1, Windows code pages, UTF-16, and encoding detection is outside
scope.

Embedded NUL bytes in a cell should result in an error rather than being
passed into ordinary R character strings.

## 14. Empty files and edge cases

Required behavior:

- an empty file returns a zero-column, zero-row `data.frame`;
- a header-only file returns zero rows with one character column for
  each header field;
- `header = FALSE` on an empty file returns a zero-column, zero-row
  `data.frame`;
- an empty data cell is `NA` under the default `na` setting;
- quoted delimiters remain part of the cell value;
- escaped quotes are unescaped by `zsv`;
- embedded newlines in quoted fields remain part of the cell value;
- CRLF and LF input must both be accepted;
- header names are never interpreted as missing values.

Blank-row behavior should be locked down with explicit fixtures matching
the chosen `zsv` parser semantics before release rather than being left
accidental.

## 15. Resource limits

The wrapper should use explicit resource checks rather than relying on
accidental allocation failure.

At minimum it must guard against:

- row counts that cannot fit in an R long vector;
- column counts beyond a package-defined safety limit;
- multiplication overflow when computing sizes;
- R vector lengths beyond `R_XLEN_T_MAX`;
- native allocation failures reported by `zsv`;
- parser limits encountered by `zsv`.

`zsv` exposes a configurable maximum column count. `zucsv` should set an
intentional internal value rather than silently inheriting an upstream
default. A reasonable initial safety cap is 65,536 columns. If real use
cases require more, the limit can later become configurable.

## 16. Interrupts and cleanup

Large reads must remain interruptible.

The native loop should call `R_CheckUserInterrupt()` periodically, for
example every 16,384 rows, rather than for every row.

All native resources must be released on both success and R errors:

- open `FILE *` handles;
- `zsv_parser` instances;
- native temporary allocations.

The implementation should structure cleanup so that R long jumps cannot
leak resources. `R_UnwindProtect()` or an equivalent disciplined cleanup
pattern should be used where needed.

## 17. Vendoring `zsv`

Vendor only the minimum library subset required by the parser.

Suggested layout:

``` text
zucsv/
├── DESCRIPTION
├── NAMESPACE
├── R/
│   └── read_csv.R
├── src/
│   ├── init.c
│   ├── read_csv.c
│   ├── convert.c
│   ├── zucsv.h
│   └── vendor/
│       └── zsv/
│           ├── include/
│           ├── src/
│           └── LICENSE
├── tests/
│   └── testthat/
├── tools/
│   └── update-zsv.sh
└── inst/
    └── LICENSE.note
```

The vendored source should be pinned to a known upstream commit or
release. Record:

- upstream repository;
- version/tag if available;
- exact commit hash;
- vendoring date;
- local patches, if any.

Avoid local modifications to upstream source where possible. Put
R-specific adaptation in `zucsv` wrapper files.

## 18. Parser mode for v0.1

Use `zsv`’s normal/default compatibility parser in the first release.

Do not expose a `parser =` argument yet. Do not enable parallel row
processing.

The newer fast/SIMD parser is an attractive future optimization, but
exposing it in v0.1 would expand the portability and testing matrix
before the basic R semantics are proven.

A later version may support:

``` r

read_csv(file, parser = "fast")
```

only if benchmarks show a meaningful end-to-end improvement after R
allocation and type conversion are included.

## 19. Build strategy

The package should always build against its vendored `zsv` copy in v0.1.

Do not search for a system-installed `zsv` library. A single known
backend version makes CRAN builds and bug reports reproducible.

Target standard R compilation environments:

- GCC on Linux;
- Apple Clang on macOS;
- GCC via Rtools on Windows.

Optional architecture-specific optimization must not be required for
correctness.

Compiler warnings should be enabled during development, and CI should
treat warnings from `zucsv` code seriously while avoiding unnecessary
edits to vendored upstream code.

## 20. Error model

Errors should identify the location and reason whenever possible.

Examples:

``` text
Cannot open CSV file: data.csv
```

``` text
CSV row 205 has 7 fields; expected 6
```

``` text
Cannot parse row 19, column 2 ("count") as integer: "1.5"
```

``` text
CSV contains an embedded NUL at row 8, column 4
```

``` text
CSV exceeds zucsv's maximum of 65536 columns
```

Do not expose raw `zsv` status codes as the primary user-facing message.
Preserve them internally where useful for diagnostics.

## 21. Testing strategy

### R-level unit tests

Test at least:

- ordinary comma-separated files;
- header/no-header input;
- single-column input;
- quoted commas;
- escaped quotes;
- embedded newlines;
- CRLF and LF line endings;
- empty cells;
- `NA` cells;
- custom `na` values;
- duplicate and empty column names;
- UTF-8 strings;
- UTF-8 BOM;
- logical inference;
- integer inference;
- integer overflow to double;
- double inference;
- scientific notation;
- character fallback;
- all-missing columns;
- forced column types;
- failed forced conversions;
- inconsistent row widths;
- header-only files;
- empty files;
- long cells;
- many rows;
- many columns up to the supported limit.

### Differential tests

For canonical, standards-compliant fixtures, compare values with
[`utils::read.csv()`](https://rdrr.io/r/utils/read.table.html) where
semantics overlap.

Differential tests should compare parsed values, not require identical
type-guessing or name-repair policies when those intentionally differ.

### Upstream behavior tests

Include a small set of fixtures exercising the real-world quoting cases
that motivated choosing `zsv`. These tests protect the R wrapper against
changes introduced when the vendored backend is upgraded.

### Sanitizers

Development CI should periodically run the native code with
AddressSanitizer and UndefinedBehaviorSanitizer where supported.

### Fuzzing

Keep the parser boundary easy to fuzz independently of R.

A future `zufuzz` harness should generate arbitrary CSV bytes and
exercise:

``` text
bytes
  ↓
zsv
  ↓
zucsv wrapper logic
  ↓
row/column/type state
```

Important invariants include:

- no crashes;
- no out-of-bounds access;
- no use-after-free;
- no leaks in repeated parse failures;
- deterministic results for identical input;
- the second pass agrees with the row/column counts established in the
  first pass.

## 22. Benchmarking

Benchmarks should measure complete R calls rather than only raw `zsv`
parser throughput.

Measure:

- elapsed time;
- parsing throughput;
- peak memory where practical;
- final object size;
- character-heavy versus numeric-heavy files;
- narrow versus wide tables;
- small versus large files.

Useful comparisons include:

``` r

utils::read.csv()
readr::read_csv()
data.table::fread()
zucsv::read_csv()
```

The goal of v0.1 is not to beat every established reader. The first
performance requirement is that the native design does not introduce
obvious avoidable overhead. Parser-only speed is less important than
end-to-end time after R strings, vectors, type conversion, and the
deliberate second pass are included.

## 23. Package dependencies

The target dependency structure is:

``` text
R
└── zucsv
    └── vendored zsv C sources
```

No runtime R package dependencies are required.

`testthat` may be used under `Suggests` for tests. Benchmark-only
packages should also remain optional.

## 24. Initial file-level implementation plan

### `R/read_csv.R`

- exported `read_csv()` wrapper;
- lightweight argument normalization;
- [`.Call()`](https://rdrr.io/r/base/CallExternal.html) invocation;
- roxygen documentation.

### `src/read_csv.c`

- file opening;
- parser configuration;
- pass 1 and pass 2 orchestration;
- row-width validation;
- data-frame construction;
- error context.

### `src/convert.c`

- NA matching;
- logical recognition;
- integer recognition/conversion;
- double recognition/conversion;
- character materialization;
- type inference state.

### `src/zucsv.h`

- internal types;
- constants;
- conversion enums;
- parser-pass state structures.

### `src/init.c`

- native routine registration.

### `src/vendor/zsv/`

- unmodified vendored parser subset;
- upstream license.

## 25. Definition of done for v0.1

The first release is complete when:

1.  `read_csv()` reads ordinary and quoted CSV files into correct R data
    frames.
2.  Header and no-header modes are stable.
3.  Basic type inference is deterministic and documented.
4.  `col_types` can force all four supported primitive types.
5.  Missing values work as documented.
6.  Row-width mismatches produce clear errors.
7.  Large reads are interruptible.
8.  All parser/file resources are cleaned up on errors.
9.  The package passes `R CMD check --as-cran` on Linux, macOS, and
    Windows.
10. Native tests pass under ASan/UBSan in development CI.
11. The vendored `zsv` version and license are documented.
12. Benchmarks confirm that the R wrapper is not introducing
    pathological overhead.

## 26. Likely v0.2 additions

After v0.1 is stable, candidates are:

- `write_csv()`;
- `read_tsv()` as a convenience wrapper;
- `parser = c("compat", "fast")`;
- raw-vector and string input;
- R connections;
- `skip` and `n_max`;
- column selection;
- explicit ragged-row handling;
- more flexible column-type specifications;
- user-configurable resource limits;
- integration with `zukomp` for compressed input.

Parallel parsing should come later than the SIMD/fast parser because
interaction with R’s single-threaded API and column materialization
requires a separate design rather than merely turning on an upstream
option.

## 27. Core design principle

The first version should keep the boundary simple:

``` text
zsv owns CSV syntax
zucsv owns R semantics
```

`zsv` should decide where rows and fields begin and end. `zucsv` should
decide what those fields mean in R, how missing values are represented,
how columns are typed, and how errors are presented.

That separation gives `zucsv` a small public API while retaining the
option to adopt more of `zsv`’s performance capabilities later without
redesigning the package.
