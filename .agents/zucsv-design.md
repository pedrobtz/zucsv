# zucsv — Design for v0.1.0

**Status:** Draft, revised 2026-09-12; §10/§13/§14/§15 corrected against the vendored parser's measured behavior (`tools/zsv-behavior.md`). Decisions are logged in §29.  
**Backend:** vendored `zsv` / `libzsv`  
**Primary API:** `read_csv()`  
**Scope:** small, read-only first release, shipped to CRAN  
**Roadmap:** `ROADMAP.md`

## 1. Purpose

`zucsv` is a small R package for reading CSV files using the `zsv` C parser as its native backend. The first release should prove the integration, establish stable R semantics, and remain easy to audit and maintain.

The package should follow the same general philosophy as the other `zu` packages: use a focused, high-quality C engine for low-level parsing while keeping R-facing behavior small and explicit.

The package name already provides the namespace, so exported functions should use conventional names rather than repeating the `zu` prefix:

```r
zucsv::read_csv("data.csv")
```

not:

```r
zucsv::zu_read_csv("data.csv")
```

## 2. Goals for v0.1

The first release should:

- read a local CSV file into a base R `data.frame`;
- use vendored `zsv` for CSV tokenization and row parsing;
- correctly handle quoted fields, embedded delimiters, embedded newlines, escaped quotes, CRLF/LF input, empty fields, and UTF-8 text;
- support files with or without a header row;
- support a configurable single-byte delimiter;
- support missing-value strings;
- infer the basic R column types `logical`, `integer`, `double`, and `character`;
- allow callers to force column types when inference is undesirable;
- provide deterministic errors for malformed table structure or failed type conversion;
- build on normal R toolchains on Linux, macOS, and Windows;
- have no mandatory R package dependencies;
- pass `R CMD check --as-cran` with no warnings or notes and be accepted on CRAN (§25);
- run on R >= 4.2.0 (see §4, `file`, for the reason).

The implementation should favor correctness, simple ownership rules, and testability over exposing every feature available in `zsv`.

## 3. Non-goals for v0.1

The first release will intentionally not provide:

- `write_csv()`;
- `read_tsv()` as a separate exported function;
- URL input;
- R connections;
- raw-vector or string input;
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
- schema metadata beyond ordinary R column types;
- a `quote` argument (`"` is always the quote character);
- comment lines;
- whitespace trimming of any kind;
- an `encoding` argument or transcoding;
- a toggle for the blank-row policy (§10 fixes it);
- tibble or `data.table` output;
- linking against a system-installed `zsv`.

These features can be considered only after the basic parser-to-R boundary is stable.

## 4. Public API

The initial public API consists of two exported functions:

```r
read_csv(
  file,
  header = TRUE,
  delimiter = ",",
  na = c("", "NA"),
  col_types = NULL
)

sniff_csv(
  file,
  delimiter = ",",
  na = c("", "NA")
)
```

### `file`

A length-one, non-missing character path to an existing local file.

For v0.1, paths are the only supported input source. Restricting input to local files makes the implementation substantially simpler and permits an exact two-pass allocation strategy.

The R wrapper applies `path.expand()` so `~/data.csv` works. The C side obtains the native path with `translateChar()` and opens it with `fopen(path, "rb")`. A directory or unreadable path is reported as `Cannot open CSV file: <path>`.

The package requires R >= 4.2.0. From that version the native encoding on Windows is UTF-8, so non-ASCII paths open correctly without a `_wfopen` code path, and every supported platform agrees that strings handed to R are UTF-8.

### `header`

A length-one logical value.

- `TRUE` (the default): the first parsed record supplies column names.
- `FALSE`: all records are data and names are generated as `V1`, `V2`, ... `Vn`.
- `NA`: detect. The first parsed record is a header unless one of its fields is
  syntactically a logical or a double under the grammars of §11. Every field being a
  name is the evidence; one numeric-looking field is enough to decide the record is
  data. A *quoted* field is skipped before either grammar is tried: quoting is the
  writer saying the field is text, which is what a column name is, so `"2024"` and
  `"TRUE"` are names where bare `2024` and `TRUE` are data (§29, decision 21). The
  rest of the file is not consulted (§29, decision 20).

Detection reads only the first record's own bytes. It consults neither `col_types` nor
`na`: a forced type cannot contradict the verdict, and a cell that the caller declared
missing still votes on its own syntax. `na` is deliberately excluded because `""` is in
the default `na` set, so letting it vote would read the ordinary header `a,,c` as data.
A file with no non-blank record has no first record to judge and resolves to `FALSE`.

Because it sees one record, detection cannot reach the verdict that comparing against
the rest of the file would in three shapes. The row count moves in both directions —
reading names as data *adds* a row, reading data as names *costs* one:

| input | `zucsv` | `fread`, DuckDB | rows vs. the other rule |
|---|---|---|---|
| `id,2024,2025` over numeric rows | data | header | one **more** |
| `NA,NA,NA` over `1,2,3` | header | data | one fewer |
| `"1","2"` over `"3","4"` | header | data | one fewer |

The first two follow from reading one record; the third is decision 21's cost. Passing
`header` explicitly is the answer in all three, and is what the documentation says.

Header values are always treated as text and are never subject to `na` matching or type inference.

### `delimiter`

A length-one character string consisting of exactly one ASCII byte (`nchar(delimiter, type = "bytes") == 1` and the byte is below `0x80`). The default is `","`.

This allows semicolon-, tab-, and pipe-delimited files without adding more exported functions in v0.1:

```r
read_csv("data.tsv", delimiter = "\t")
read_csv("data.txt", delimiter = ";")
```

Newline, carriage return, form feed, NUL, the double quote, and any multi-byte character are rejected as delimiters.

### `na`

A character vector of exact cell values that should become missing values, or `NULL`. The default is:

```r
c("", "NA")
```

Matching occurs after CSV unquoting and is a bytewise comparison against the UTF-8 form of each `na` element (the C side translates `na` with `translateCharUTF8()` once). Therefore quoted and unquoted forms are treated identically:

```text
NA
"NA"
```

both become missing values under the default configuration.

`na = NULL` disables missing-value matching. `NA_character_` elements in `na` are an error.

No whitespace trimming is performed before matching in v0.1.

### `col_types`

Controls column conversion.

`NULL` means infer column types from the complete file.

A character vector may force types. Supported values are exactly:

```text
logical
integer
double
character
```

A length-one value applies to every column:

```r
read_csv("codes.csv", col_types = "character")
```

A vector with one value per column specifies each column explicitly:

```r
read_csv(
  "data.csv",
  col_types = c("integer", "character", "double")
)
```

Unknown type names are rejected by the R wrapper before the file is opened. The length is checked in C once the column count is known:

```text
col_types has length 3 but the CSV has 5 columns
```

No shorthand type codes, no aliases such as `"numeric"`, and no named partial specifications are supported in v0.1.

### `sniff_csv()`

`sniff_csv()` runs pass 1 and stops, returning what that pass learned instead of
materialising columns: the header verdict, `ncol`, `nrow`, `col_names` and
`col_types`. It is the answer to the tension in §28 between "`zucsv` owns R
semantics, strictly" and an argument that guesses — the guess becomes something
you can look at and then *stop making*:

```r
s <- sniff_csv(path)
read_csv(path, header = s$header, col_types = s$col_types)
```

The second call infers nothing. It is a strict read against a schema the caller
now owns, and it gives the same answer next month whatever the data does.

Three properties are load-bearing:

- **It is the same pass.** `sniff_csv()` and `read_csv()` share `zucsv_run()` and
  `zucsv_read_body()`; sniffing is an early return once inference is final, not a
  second implementation. A sniffer that ran its own inference could describe a file
  differently from how the reader would read it, which would make it worse than
  useless (§29, decision 22).
- **It validates.** Pass 1 is where ragged rows, embedded NULs and invalid UTF-8
  are caught, so `sniff_csv()` errors on them exactly as `read_csv()` does. A file
  that sniffs cleanly cannot fail `read_csv()` on content.
- **It costs the expensive pass**, not a cheap sample. It is for pinning decisions
  down, not for calling before every read.

The return value is a plain `list`, not an S3 object: it composes straight into
`read_csv()`, and the type names it reports are `typeof()` spellings, so
`col_types` needs no translation. An empty file reports `header = FALSE` with zero
columns and zero rows.

`sniff_csv()` does not detect the delimiter. Nothing in `zucsv` does — that is a
separate guess with its own failure modes, and §3 keeps it out of v0.1.

## 5. Return value

`read_csv()` returns an ordinary base R `data.frame`.

The implementation constructs the data frame directly as a named list of equal-length vectors and assigns:

- `names`: header names (or `V1..Vn`) as UTF-8 `CHARSXP`s;
- `row.names`: the compact form `c(NA_integer_, -nrow)`, or `integer(0)` when there are no rows;
- `class`: `"data.frame"`.

It should not route through `data.frame()` and should not perform factor conversion.

Character cells become `CHARSXP`s created with `mkCharLenCE(ptr, len, CE_UTF8)`; missing cells are `NA_STRING`, `NA_LOGICAL`, `NA_INTEGER`, or `NA_REAL` according to the column type.

Column names from a header are preserved exactly in v0.1, including duplicates and empty names. No automatic name repair is performed. This avoids silently changing information from the source file and keeps name policy outside the CSV parser.

Examples:

```csv
id,name,score
1,Ada,9.5
2,Linus,8.0
```

becomes conceptually:

```r
data.frame(
  id = c(1L, 2L),
  name = c("Ada", "Linus"),
  score = c(9.5, 8.0),
  check.names = FALSE
)
```

## 6. Why `zsv`

`zsv` is used as the parsing engine rather than implementing CSV syntax in the R package.

The relevant characteristics for `zucsv` are:

- a C library API intended for embedding;
- streaming parsing;
- direct access to parsed cells as pointer/length pairs;
- configurable delimiter handling;
- explicit handling of real-world quoting and newline cases;
- low memory overhead;
- a pull API that maps naturally to an R row-processing loop;
- the possibility of using newer fast/SIMD parsing modes in a later release without changing the R-level API.

Only the library code required for parsing is vendored. The `zsv` command-line application and unrelated facilities such as SQL, SQLite, JSON conversion, interactive viewing, and other CLI commands are not part of `zucsv`.

## 7. Native interface

The package uses the base R C API and `.Call` directly. It should not depend on Rcpp or cpp11.

The exported R function validates simple arguments and calls one registered C routine, conceptually:

```c
SEXP C_read_csv(
    SEXP file,
    SEXP header,
    SEXP delimiter,
    SEXP na,
    SEXP col_types
);
```

Rules for the native layer:

- every C file defines `R_NO_REMAP` before including R headers and uses `Rf_`-prefixed names;
- only entry points documented in *Writing R Extensions* are used (`R CMD check` on R >= 4.5 flags non-API entry points);
- `src/init.c` registers the routine in an `R_CallMethodDef` table via `R_registerRoutines()`, then calls `R_useDynamicSymbols(dll, FALSE)` and `R_forceSymbols(dll, TRUE)`;
- `R/zucsv-package.R` carries `@useDynLib zucsv, .registration = TRUE`, which makes the registered routine available as the R object `C_read_csv`;
- `PROTECT`/`UNPROTECT` usage must be balanced on every path so the code is clean under `rchk` (§25).

The R wrapper remains intentionally thin:

```r
read_csv <- function(file,
                     header = TRUE,
                     delimiter = ",",
                     na = c("", "NA"),
                     col_types = NULL) {
  # argument checks producing R-level errors, then:
  file <- path.expand(file)
  .Call(C_read_csv, file, header, delimiter, na, col_types)
}
```

Argument validation that affects native safety must be repeated in C; the R checks exist to give good messages, not to protect the C code.

## 8. Parser API choice

`zucsv` should use `zsv`'s pull parsing API for v0.1:

```c
while (zsv_next_row(parser) == zsv_status_row) {
    size_t n = zsv_cell_count(parser);

    for (size_t i = 0; i < n; ++i) {
        struct zsv_cell cell = zsv_get_cell(parser, i);
        /* consume cell.str / cell.len */
    }
}
```

The pull API is preferred because it gives the R wrapper an ordinary sequential control flow, makes error handling easier, and avoids invoking R-oriented logic from parser callbacks.

The parser is fed from the `FILE *` through `zsv`'s read callback (`fread`-compatible): `zsv_next_row()` pulls its own input from `opts.stream` and must not be mixed with `zsv_parse_bytes()` or `zsv_parse_more()`. The pinned commit provides the pull API; it returns `zsv_status_row` per record and `zsv_status_done` at end of input.

The push API can be benchmarked later, but it is not needed to establish the package architecture.

## 9. Two-pass reading strategy

v0.1 should use two complete parsing passes over the file.

This is a deliberate simplification enabled by accepting only local file paths.

### Pass 1: inspect and validate

The first pass does *all* validation so that pass 2 cannot fail on the content of the file:

1. skips a UTF-8 BOM (§13) and reads the header, if present;
2. determines the expected number of columns and checks it against the column cap and `col_types` length;
3. counts data records, skipping blank records (§10);
4. verifies that every data record has the expected width;
5. rejects embedded NUL bytes;
6. applies `na` matching;
7. infers column types when `col_types = NULL`, or validates every non-missing cell against the forced type;
8. validates NUL and UTF-8 in cells no grammar accepted (§13) — a cell any
   grammar takes is ASCII by construction, so numeric and logical columns
   pay nothing for this;
9. checks size limits (§15) before any R vector is allocated.

No R data frame is constructed during this pass.

### Pass 2: materialize

The second pass reopens or rewinds the file and:

1. allocates each R column at its exact final length;
2. parses the file again;
3. converts each cell directly into the destination R vector;
4. constructs character strings only for character columns;
5. assigns names, row names, and the `data.frame` class.

Pass 2 must re-check the record count and widths against pass 1 as it goes and error with `CSV file changed while it was being read` if they disagree. That keeps a concurrent modification from turning into an out-of-bounds write. Any other conversion failure in pass 2 is an internal error, because pass 1 already validated the content.

The resulting data path is:

```text
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

This avoids growable R columns, repeated vector copying, temporary row lists, and complicated type widening during materialization.

The cost is parsing the file twice and running the numeric grammar checks twice. For the initial implementation, the simpler allocation and ownership model is worth that tradeoff. Benchmarks should later determine whether a one-pass column builder is worthwhile.

## 10. Records, column width, and blank rows

### Record numbering

Error messages refer to *records*, not lines, because a quoted field can span lines. Records are numbered from 1 in the order `zsv` emits them, including the header record and skipped blank records. So with `header = TRUE` the first data record is record 2. This matches what a user sees when stepping through the file with a CSV-aware viewer.

### Column width

The expected number of columns is established by:

- the header record when `header = TRUE`, or when `header = NA` and the first
  non-blank record is detected as names;
- the first non-blank data record otherwise.

Every subsequent non-blank record must contain exactly that number of cells.

A mismatch is an error:

```text
CSV row 42 has 6 fields; expected 5
```

Although `zsv` itself can parse tables with inconsistent row widths, `zucsv` v0.1 deliberately requires a rectangular table because every R data-frame column must have the same length.

Future versions may add an explicit ragged-row policy such as padding short rows with `NA`, but v0.1 must not silently invent that policy.

### Blank rows

A *blank record* is a record with no bytes at all between its line terminators (a trailing terminator at end of file does not create a record). Blank records are skipped everywhere: they do not count as data rows and do not trigger a width error. This makes files that end in `\n\n`, or that contain an empty line between blocks, read without complaint, and matches the default of the other common R readers.

A blank record is recognised as one cell, of length zero, that was **not**
quoted. The parser reports quoting per cell, so `""` on a line of its own is
a one-cell record that is *not* blank and is kept. Consequences to document
and test:

- a record containing only whitespace or only delimiters is *not* blank; it is a record of one or more empty cells and is subject to the width check;
- in a single-column file an intentional empty cell must be written as `""` to survive; an unquoted empty line is skipped;
- with `header = TRUE`, the header is the first non-blank record, and with
  `header = NA` it is that same record that detection judges.

`zucsv` decides what counts as a blank record, not the parser. The parser is
opened with `keep_empty_header_rows = 1` because its own default drops every
*leading* record whose cells are all zero-length, using a blankness test that
ignores quoting -- which would hide a first record of empty names, and a
leading `""`, from the policy above.

The parser does not skip blank lines itself — it emits them as one-cell
records — so this policy is `zucsv`'s own. It holds regardless of what the
parser emits; `tools/zsv-behavior.md` records what it actually does, and
`tests/testthat/test-upstream.R` enforces that the two stay consistent.

## 11. Type inference

When `col_types = NULL`, inference uses all non-missing values in each column during pass 1.

For each column, track whether every observed non-missing value can be represented as:

- logical;
- integer;
- double.

The final type is selected in this order:

```text
all logical  → logical
all integer  → integer
all numeric  → double
otherwise    → character
```

A column containing only missing values becomes `character` in v0.1 because there is no evidence for a narrower type.

Grammar checks operate on the raw `{pointer, length}` cell without copying or modifying it. A cell is only converted once its column type is fixed (pass 2).

### Logical syntax

Recognize only these four exact strings:

```text
TRUE
FALSE
true
false
```

Values `T`, `F`, `True`, `0`, `1`, `yes`, and `no` are not logical syntax in v0.1.

### Integer syntax

```text
[+-]? [0-9]+
```

Base-10, optional leading sign, no grouping separator, leading zeros permitted (`007` is `7L`, as with `utils::type.convert()`).

The value must lie in `[-2147483647, 2147483647]`; `-2147483648` is `NA_integer_` in R and is out of range. Conversion accumulates into a 64-bit integer with an overflow check, so no libc call is needed. A syntactically integral value outside the range promotes the column to double during inference and is a conversion error when `integer` is forced.

Examples: `0`, `42`, `-17`, `+9`.

### Double syntax

```text
[+-]? ( [0-9]+ ( '.' [0-9]* )? | '.' [0-9]+ ) ( [eE] [+-]? [0-9]+ )?
| [+-]? 'Inf'
| 'NaN'
```

Using `.` as the decimal separator. `Inf`, `-Inf`, `+Inf`, and `NaN` are accepted, case-sensitively, because `write.csv()` produces them and base R output must round-trip. `inf`, `Infinity`, `nan`, hexadecimal floats, and leading or trailing whitespace are not double syntax.

Conversion happens only after the grammar check passes. It is a transcription of the decimal branch of R's own `R_strtod5()` working directly on the `{pointer, length}` cell (`src/strtod_body.h`), so values match `as.numeric()` bit for bit and do not depend on `LC_NUMERIC`, with none of the copy, terminator, call, or whitespace/`NA`/hex handling that `R_strtod()` would repeat. R accumulates in `LDOUBLE` and does not tell packages whether that is `long double` or `double`, so the transcription is instantiated for both and the first call keeps whichever agrees with `R_strtod()` on a set of probe values -- falling back to calling `R_strtod()` per cell if neither does. Verified bit-identical on 8,000,051 inputs (§29, decision 15).

Examples: `1.5`, `-0.25`, `6.02e23`, `1E-8`, `.5`, `5.`.

Locale-specific decimal commas and thousands separators are not supported in v0.1.

### Character fallback

Any non-missing value that does not satisfy the selected primitive types makes the inferred column `character`.

Type inference must not mutate the original text representation before the final type is known.

## 12. Forced type conversion

When `col_types` forces a type, every non-missing cell must be valid for that type.

Conversion failure is an error, not a warning followed by `NA`:

```text
Cannot parse row 18, column 3 ("amount") as double: "12 USD"
```

The column name is the header name or the generated `V<n>`. The offending value is truncated in the message (§20).

This strict behavior prevents silent data loss and keeps v0.1 semantics easy to reason about.

`character` always accepts a non-NUL, valid-UTF-8 cell.

## 13. Text and encoding

v0.1 treats text input as UTF-8.

A UTF-8 BOM (`EF BB BF`) at the beginning of the file is ignored before interpreting the first field or header name. The vendored parser already strips it, so `zucsv` adds no BOM handling of its own, but it guarantees the result and covers it with a fixture either way.

Strings handed to R are created with `mkCharLenCE(..., CE_UTF8)`. Cells that fail the primitive grammars — the only cells that can carry non-ASCII bytes into R — and header names are validated as UTF-8 in pass 1. Invalid sequences are an error:

```text
CSV contains invalid UTF-8 at row 8, column 4 ("name")
```

Erroring is preferred over marking bad bytes as UTF-8, which would defer the failure to a later `nchar()` or `print()` with a far less useful message. An `encoding` argument is the v0.2 escape hatch.

No encoding argument or transcoding is provided in v0.1. Supporting Latin-1, Windows code pages, UTF-16, and encoding detection is outside scope.

Embedded NUL bytes in a cell are an error rather than being passed into ordinary R character strings.

## 14. Empty files and edge cases

Required behavior:

- an empty file returns a zero-column, zero-row `data.frame`;
- a file consisting only of blank records is treated as empty;
- a header-only file returns zero rows with one character column for each header field;
- `header = FALSE` on a one-record file returns one row of data;
- `header = NA` on a one-record file returns either one row of data or zero
  rows and that record's names, by the rule in §4;
- a record of empty names (`,`) is a header like any other, and an all-empty
  first record is not silently dropped;
- an empty data cell is `NA` under the default `na` setting, whether written as nothing or as `""`;
- with `na = NULL` an empty cell is the empty string and makes its column `character`;
- a trailing delimiter (`a,b,`) yields a final empty cell, so the record has three fields;
- quoted delimiters remain part of the cell value;
- escaped quotes (`""` inside a quoted field) are unescaped by `zsv`;
- embedded newlines in quoted fields remain part of the cell value, including a CR LF pair and a lone CR;
- CRLF and LF input must both be accepted; a final record without a trailing terminator is still a record;
- header names are never interpreted as missing values;
- `" 42"` (leading space) is not numeric syntax and makes its column `character`.

Blank-record (§10), BOM, and unbalanced-quote behavior are established in
`tools/zsv-behavior.md` and pinned by fixtures rather than left accidental.
An unbalanced quote is not an error from the parser: it swallows the rest of
the line into one cell, which `zucsv` then reports as a row-width mismatch.

## 15. Resource limits

The wrapper should use explicit resource checks rather than relying on accidental allocation failure.

At minimum it must guard against:

- more than `2147483647` data rows — the practical `data.frame` limit, because compact row names and `nrow()` are integers (this is stricter than `R_XLEN_T_MAX`);
- more than the package column cap;
- multiplication overflow when computing sizes;
- native allocation failures reported by `zsv` (`zsv_new()` returning `NULL`, out-of-memory statuses);
- parser limits encountered by `zsv`, in particular its maximum row size.

`zsv` exposes configurable maximum column and row sizes, and **silently
truncates** when either is exceeded — no error status, no way to tell the
truncated result from a genuine one. `zucsv` must detect both itself
(`tools/zsv-behavior.md` has the measurements):

- **Columns.** Leaving `max_columns` at 0 gets the upstream default of 1024,
  so it is always set explicitly. `zucsv` sets it to its cap *plus one*
  (65537) and errors when a record reports more than 65,536 cells
  (`CSV exceeds zucsv's maximum of 65536 columns`). The extra slot is what
  separates a file at the cap from one over it.
- **Record size.** An oversize row is emitted as a record with **zero**
  cells. Since even a blank line yields one cell, a zero-cell record is an
  unambiguous overflow signal, and `zucsv` raises an error naming the record.
  The limit is 8 MiB. It is not free to set this high: the parser sizes its
  scan buffer at twice `max_row_size` and `malloc`s it on every parse, so a
  huge limit is a memory cost on every call, including on tiny files. 8 MiB
  is 128x the upstream default of 64 KiB, covers any realistic row, and
  leaves a 16 MiB buffer whose pages stay untouched unless rows really do
  get that large.

If real use cases require more, the limits can later become configurable.

## 16. Interrupts and cleanup

Large reads must remain interruptible.

The native loops in both passes call `R_CheckUserInterrupt()` periodically, for example every 16,384 records, rather than for every record.

All native resources must be released on both success and R errors:

- open `FILE *` handles;
- `zsv_parser` instances;
- native temporary allocations.

The cleanup discipline for v0.1:

- all `zucsv`-owned scratch memory (per-column inference state, the translated `na` table, the numeric scratch buffer) is allocated with `R_alloc()`, which R frees automatically when `.Call` returns or unwinds — so it never needs explicit cleanup;
- the two resources that are not R-managed, the `FILE *` and the `zsv_parser`, live in a single reader struct;
- the parse runs inside `R_UnwindProtect()` with a cleanup function that closes and deletes whatever is non-`NULL` in that struct and then continues the unwind with `R_ContinueUnwind()`. `Rf_error()` and `R_CheckUserInterrupt()` both long-jump, so every error and interrupt goes through this cleanup.

## 17. Vendoring `zsv`

Vendor only the minimum library subset required by the parser.

Layout:

```text
zucsv/
├── DESCRIPTION
├── NAMESPACE
├── NEWS.md
├── LICENSE                     # MIT template for zucsv itself
├── LICENSE.md
├── LICENSE.note                # points out the bundled zsv license
├── cran-comments.md
├── R/
│   ├── read_csv.R
│   └── zucsv-package.R
├── src/
│   ├── Makevars
│   ├── Makevars.win
│   ├── init.c
│   ├── read_csv.c
│   ├── convert.c
│   ├── zucsv.h
│   └── vendor/
│       └── zsv/
│           ├── UPSTREAM        # repo, tag, commit, date, patch list
│           ├── LICENSE
│           ├── include/
│           └── src/
├── inst/
│   └── COPYRIGHTS              # copyright holders of the bundled code
├── tests/
│   └── testthat/
└── tools/
    ├── update-zsv.sh           # re-vendors a given commit and re-applies patches
    └── patches/                # minimal, documented patches to upstream
```

The vendored source is pinned to a known upstream commit or release. `src/vendor/zsv/UPSTREAM` records:

- upstream repository;
- version/tag if available;
- exact commit hash;
- vendoring date;
- local patches, each with a one-line reason.

Avoid local modifications to upstream source where possible. Put R-specific adaptation in `zucsv` wrapper files. Where a patch is unavoidable — typically for CRAN compliance (§25) — keep it as a file in `tools/patches/` so `tools/update-zsv.sh` can re-apply it on the next upgrade, and report it upstream when it is a genuine fix.

Vendoring must include an audit of the subset for:

- calls CRAN forbids in package code: `exit`, `abort`, `assert` (compiles to `abort` unless `NDEBUG`), `printf`/`puts`/`fprintf(stderr, ...)`, `rand`/`srand`;
- compiler warnings under `-Wall -pedantic -Wstrict-prototypes` with GCC and Clang, and under C23 (`R CMD check` on R-devel);
- anything requiring `-march`/`-mavx*` flags or a non-standard `-std=` for correctness;
- GNU make constructs, since `src/Makevars` must work with a POSIX `make` unless `SystemRequirements: GNU make` is declared;
- the behaviors `zucsv` relies on, re-verified and written back to `tools/zsv-behavior.md`: how the pull API is driven, empty-line records, the quoted-cell flag, BOM handling, the two silent-truncation paths, and unbalanced-quote recovery.

## 18. Parser mode for v0.1

Use `zsv`'s normal/default compatibility parser in the first release.

Do not expose a `parser =` argument yet. Do not enable parallel row processing.

The newer fast/SIMD parser is not usable, and not merely deferred. It is unreachable from the pull API — `zsv_next_row()` overwrites `scan_engine` — and, reached through the push API, it returns **raw** cells: surrounding quotes kept, doubled quotes not unescaped, and a quoted empty cell arriving as two bytes rather than zero, which silently changes §10's blank-record rule. `zsv_set_column_filter()` claims to restore normalization for selected columns; measured over every column, it changes nothing. Using it would mean zucsv stripping quotes and unescaping itself — CSV syntax, which §28 assigns to `zsv`. `tools/zsv-behavior.md` records the measurements and what to re-check on upgrade.

A later version may support:

```r
read_csv(file, parser = "fast")
```

only if benchmarks show a meaningful end-to-end improvement after R allocation and type conversion are included.

## 19. Build strategy

The package always builds against its vendored `zsv` copy in v0.1.

Do not search for a system-installed `zsv` library. A single known backend version makes CRAN builds and bug reports reproducible.

Target standard R compilation environments:

- GCC on Linux;
- Apple Clang on macOS;
- GCC via Rtools on Windows.

`src/Makevars` rules:

- list every object file explicitly in `OBJECTS` (including `vendor/zsv/src/*.o`); no `$(wildcard)`, `$(shell)`, or other GNU make features;
- set include paths through `PKG_CPPFLAGS` (`-I. -Ivendor/zsv/include`) and any feature macros `zsv` needs;
- never set optimization or architecture flags (`-O3`, `-march=native`, `-mavx2`) and never suppress diagnostics (`-w`, `-Wno-*`) — CRAN rejects both;
- `Makevars.win` mirrors `Makevars`.

Optional architecture-specific optimization must not be required for correctness.

Compiler warnings should be enabled during development (`-Wall -Wextra -pedantic -Wstrict-prototypes` via `~/.R/Makevars`), and CI should treat warnings from `zucsv` code as failures. Warnings from vendored code that surface under CRAN's flags are fixed with patches (§17), not silenced.

## 20. Error model

Errors should identify the location and reason whenever possible.

Examples:

```text
Cannot open CSV file: data.csv
```

```text
CSV row 205 has 7 fields; expected 6
```

```text
Cannot parse row 19, column 2 ("count") as integer: "1.5"
```

```text
CSV contains an embedded NUL at row 8, column 4 ("notes")
```

```text
CSV contains invalid UTF-8 at row 8, column 4 ("name")
```

```text
CSV exceeds zucsv's maximum of 65536 columns
```

```text
col_types has length 3 but the CSV has 5 columns
```

Conventions:

- "row" means record as defined in §10;
- columns are 1-based and carry the column name in parentheses when one exists;
- cell values quoted in a message are truncated to 40 bytes with a trailing `...` and never include NUL bytes;
- all errors are raised with `Rf_error()` after native cleanup (§16), never with `Rf_warning()` followed by `NA`.

Do not expose raw `zsv` status codes as the primary user-facing message. Preserve them internally where useful for diagnostics (for example by appending `zsv_parse_status_desc()` output to allocation and parser-limit errors).

## 21. Testing strategy

Fixtures are written by tests into `tempfile()` with `writeBin()` so line endings, BOMs, NULs, and invalid bytes are exact; there is no `withr` or `readr` dependency in `Suggests`.

### R-level unit tests

Test at least:

- ordinary comma-separated files;
- header, no-header and detected-header input;
- single-column input;
- quoted commas;
- escaped quotes;
- embedded newlines, LF and CRLF;
- CRLF and LF line endings, with and without a final terminator;
- trailing delimiter;
- blank records: trailing, interior, leading, and an all-blank file;
- whitespace-only and delimiter-only records (width errors);
- empty cells, unquoted and `""`;
- `NA` cells;
- custom `na` values and `na = NULL`;
- duplicate and empty column names;
- UTF-8 strings in cells, header names, and the file path (including `~` expansion);
- UTF-8 BOM;
- invalid UTF-8 (error);
- embedded NUL (error);
- logical inference and the rejected spellings (`T`, `True`, `1`);
- integer inference, leading zeros, `+` sign;
- integer overflow to double, including `-2147483648`;
- double inference, `.5`, `5.`, scientific notation, `Inf`, `-Inf`, `NaN`;
- character fallback, including `" 42"`;
- all-missing columns;
- forced column types for all four types, length 1 and length `ncol`;
- `col_types` length mismatch and unknown names;
- failed forced conversions, with the exact error message;
- inconsistent row widths, with the exact error message;
- header-only files;
- empty files;
- invalid `delimiter` values;
- long cells;
- many rows (modest on CRAN; larger under `skip_on_cran()`);
- many columns up to the supported limit, and one beyond it.

### Differential tests

For canonical, standards-compliant fixtures, compare values with `utils::read.csv(..., check.names = FALSE, stringsAsFactors = FALSE, na.strings = c("", "NA"))` where semantics overlap.

Differential tests should compare parsed values, not require identical type-guessing or name-repair policies when those intentionally differ.

A randomized round-trip test (`write.csv()` of generated data frames → `read_csv()` → `identical()`) exercises the numeric grammars and quoting cheaply; keep it deterministic with a fixed seed.

### Conformance tests

`csv-spectrum` (<https://github.com/max-mapper/csv-spectrum>, BSD-2-Clause)
is the de facto acid test for CSV parsers: quoted commas, escaped quotes,
embedded newlines with both LF and CRLF, empty fields, UTF-8, and JSON
embedded in cells. `zucsv` passes 11 of its 12 fixtures.

The twelfth, `location_coordinates`, is excluded because it is inconsistent
with itself — its CSV and its JSON give different phone numbers, the JSON is
a bare object where every other fixture is an array, and the CSV has a bare
`"` inside an unquoted field. Three open upstream issues cover it.

`tools/update-csv-spectrum.R` regenerates `tests/testthat/test-spectrum.R`
from a pinned upstream commit. The fixtures are transcribed as raw byte
vectors rather than vendored as files: upstream keeps its CRLF fixtures
correct through `.gitattributes`, which would not survive being copied here,
and raw bytes in an R file cannot be normalised by a checkout, an editor, or
`core.autocrlf`.


`csv-test-data` (<https://github.com/sineemore/csv-test-data>) is a second,
stricter RFC 4180 suite, including seven deliberately malformed files that a
parser is meant to reject. `zucsv` agrees with 18 of its 25 fixtures. It is
**not** transcribed into the test suite: the upstream repository declares no
licence, so its files may not be redistributed. `tools/check-csv-test-data.R`
clones it at run time instead and nothing from it ships.

That script is a regression check rather than a report: every fixture has a
recorded expectation, the seven deviations included, and it exits non-zero if
reality stops matching. The deviations are all decisions taken elsewhere in
this document:

| fixture | why we differ |
|---|---|
| `all-empty`, `empty-one-column` | blank records are skipped everywhere (§10, decision 2); the suite wants a blank line to be a row of one empty field |
| `header-no-rows` | representational: a header-only file gives zero rows but keeps its columns (§14), which the suite's `[]` cannot express |
| `bad-header-no-header` | an empty file is a 0×0 data frame (§14); `zucsv` has no notion of a *required* header |
| `bad-header-wrong-header` | the suite checks the header equals `foo,bar,baz` — schema validation, not CSV parsing |
| `bad-quotes-with-unescaped-quote`, `bad-unescaped-quote` | a quote inside an unquoted cell is passed through, as Excel does (§14); only `zsv`'s unused SIMD engine rejects it |

Only the last of these is a genuine strictness difference from RFC 4180, and
it follows from §18's choice of the compatibility parser.

### Upstream behavior tests

Include a small set of fixtures exercising the real-world quoting cases that motivated choosing `zsv`, plus the confirmed empty-line, BOM, and unbalanced-quote behaviors from §17. These tests protect the R wrapper against changes introduced when the vendored backend is upgraded.

### Sanitizers and static analysis

Development CI runs the native code under AddressSanitizer and UndefinedBehaviorSanitizer (e.g. the `rocker/r-devel-san` image), and the `PROTECT` discipline is checked with `rchk` before every CRAN submission (R-hub offers both).

### Fuzzing

Keep the parser boundary easy to fuzz independently of R.

A future `zufuzz` harness should generate arbitrary CSV bytes and exercise:

```text
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
- the second pass agrees with the row/column counts established in the first pass.

## 22. Benchmarking

Benchmarks should measure complete R calls rather than only raw `zsv` parser throughput.

Measure:

- elapsed time;
- parsing throughput;
- peak memory where practical;
- final object size;
- character-heavy versus numeric-heavy files;
- narrow versus wide tables;
- small versus large files.

Useful comparisons include:

```r
utils::read.csv()
readr::read_csv()
data.table::fread()
zucsv::read_csv()
```

Benchmark scripts live outside the package build (`bench/`, listed in `.Rbuildignore`) so the comparison packages never enter `Suggests`.

The goal of v0.1 is not to beat every established reader. The first performance requirement is that the native design does not introduce obvious avoidable overhead. Parser-only speed is less important than end-to-end time after R strings, vectors, type conversion, and the deliberate second pass are included.

## 23. Package dependencies

The target dependency structure is:

```text
R (>= 4.2.0)
└── zucsv
    └── vendored zsv C sources
```

No runtime R package dependencies are required.

`testthat` is the only `Suggests` entry needed for tests. Benchmark-only packages stay out of `DESCRIPTION` entirely (§22).

## 24. Initial file-level implementation plan

### `R/read_csv.R`

- exported `read_csv()` wrapper;
- lightweight argument normalization and R-level errors for bad argument shapes;
- `path.expand()`;
- `.Call()` invocation;
- roxygen documentation with `@return` and runnable `@examples`.

### `src/read_csv.c`

- `C_read_csv()` entry point;
- reader struct, `R_UnwindProtect()` body and cleanup;
- file opening and parser configuration;
- pass 1 and pass 2 orchestration;
- record numbering, blank-record skipping, width validation;
- data-frame construction;
- error context and message formatting.

### `src/convert.c`

- NA table construction and matching;
- logical, integer, and double grammar checks on `{pointer, length}` cells;
- integer accumulation with overflow detection;
- `R_strtod()` conversion via scratch buffer;
- UTF-8 validation;
- character materialization;
- per-column inference state and final type selection.

### `src/zucsv.h`

- internal types;
- constants (column cap, record-size cap, interrupt interval, message truncation length);
- conversion enums;
- parser-pass state structures.

### `src/init.c`

- native routine registration.

### `src/Makevars`, `src/Makevars.win`

- explicit object list and include paths (§19).

### `src/vendor/zsv/`

- vendored parser subset, upstream license, `UPSTREAM` record.

## 25. CRAN requirements

CRAN acceptance is a v0.1 goal, and several of its policies shape the code rather than just the paperwork. Consolidated here so nothing is discovered at submission time.

### Metadata and licensing

- `Title` in title case, no trailing period; `Description` is a paragraph that names `'zsv'` in single quotes with its URL in angle brackets and does not start with "This package".
- `Authors@R` lists the `zsv` copyright holder with `role = "cph"`; `inst/COPYRIGHTS` states which files are theirs and under which license; `LICENSE.note` points there. CRAN requires copyright of bundled code to be unambiguous from `DESCRIPTION` alone.
- `LICENSE` holds only the MIT `YEAR`/`COPYRIGHT HOLDER` lines; `LICENSE.md` stays in `.Rbuildignore`.
- `Depends: R (>= 4.2.0)`; `URL` and `BugReports` set.
- `.Rbuildignore` covers every non-standard top-level file (`zucsv-design.md`, `ROADMAP.md`, `CLAUDE.md`, `bench/`, `_pkgdown.yml`, `.github/`).

### Compiled code

- No `exit`, `abort`, `assert`, `printf`-family output to stdout/stderr, or `rand` anywhere under `src/`, vendored code included.
- Only documented R API entry points; `R_NO_REMAP` everywhere.
- Clean under `-Wall -pedantic -Wstrict-prototypes` on GCC and Clang, under C23, and under `_R_CHECK_COMPILATION_FLAGS_`; no flag or diagnostic suppression in `Makevars`.
- Clean under ASan/UBSan, valgrind, and `rchk`. CRAN runs these after acceptance and asks for fixes within two weeks.
- No GNU make features without `SystemRequirements: GNU make`.

### Documentation and tests

- Every exported function has `\value` (roxygen `@return`) and examples that run in under a few seconds without network or non-temp files.
- Tests write only under `tempdir()` and finish quickly on CRAN; large inputs are behind `skip_on_cran()`.
- `NEWS.md` and `cran-comments.md` exist; `README.md` has a real example.

### Process

- `R CMD check --as-cran` on current R-devel is clean (no WARNINGs, no NOTEs beyond "new submission") on Linux, macOS, and Windows before submitting.
- First submissions get a human review; the usual requests concern `Description` wording, `\value`, examples, and bundled-code copyright — all covered above.

## 26. Definition of done for v0.1

The first release is complete when:

1. `read_csv()` reads ordinary and quoted CSV files into correct R data frames.
2. Header, no-header and detection (`header = NA`) modes are stable.
3. Basic type inference is deterministic and documented, and `sniff_csv()` reports it.
4. `col_types` can force all four supported primitive types.
5. Missing values work as documented.
6. Row-width mismatches, NULs, and invalid UTF-8 produce the documented errors.
7. Blank-record, BOM, and unbalanced-quote behavior are pinned by fixtures.
8. Large reads are interruptible.
9. All parser/file resources are cleaned up on errors.
10. The package passes `R CMD check --as-cran` on R-devel on Linux, macOS, and Windows.
11. Native code passes ASan/UBSan, valgrind, and `rchk`.
12. The vendored `zsv` version, patches, and license are documented (`UPSTREAM`, `inst/COPYRIGHTS`).
13. Benchmarks confirm that the R wrapper is not introducing pathological overhead.
14. The package is accepted on CRAN.

## 27. Likely v0.2 additions

After v0.1 is stable, candidates are:

- `write_csv()`;
- `read_tsv()` as a convenience wrapper;
- `parser = c("compat", "fast")`;
- raw-vector and string input;
- R connections;
- `skip` and `n_max`;
- column selection;
- explicit ragged-row handling;
- an `encoding` argument;
- more flexible column-type specifications;
- user-configurable resource limits;
- integration with `zukomp` for compressed input.

Parallel parsing should come later than the SIMD/fast parser because interaction with R's single-threaded API and column materialization requires a separate design rather than merely turning on an upstream option.

## 28. Core design principle

The first version should keep the boundary simple:

```text
zsv owns CSV syntax
zucsv owns R semantics
```

`zsv` should decide where rows and fields begin and end. `zucsv` should decide what those fields mean in R, how missing values are represented, how columns are typed, and how errors are presented.

That separation gives `zucsv` a small public API while retaining the option to adopt more of `zsv`'s performance capabilities later without redesigning the package.

## 29. Decision log

Decisions taken in the 2026-09-12 revision that were open or unstated in the first draft. Each is reversible before implementation starts; after that, changing one means changing fixtures.

| # | Decision | Alternative rejected | Why |
|---|----------|----------------------|-----|
| 1 | `Depends: R (>= 4.2.0)` | Support R 4.0/4.1 | UTF-8 native encoding on Windows makes non-ASCII paths and strings uniform; avoids a `_wfopen` branch. |
| 2 | Blank records are skipped everywhere (§10) | Treat a blank line as a one-field record (width error in multi-column files) | Files ending in `\n\n` are common; readr and `fread` skip by default; the single-column ambiguity is documented and resolved by quoting `""`. |
| 3 | `Inf`, `-Inf`, `+Inf`, `NaN` are double syntax (§11) | Character fallback | `write.csv()` emits them; base R output must round-trip. Case-sensitive to stay strict. |
| 4 | Leading zeros are integer syntax (§11) | Character fallback to protect ZIP codes | Matches `utils::type.convert()`; `col_types = "character"` is the documented escape. |
| 5 | Numeric conversion is identical to `as.numeric()` (§11) | libc `strtod`, a correctly-rounded parser | Correctly rounded would be *more* accurate than base R -- `as.numeric("0.799012")` is one ULP off on x87 -- but it would differ from `read.csv()` on ~0.02% of ordinary decimals, and no other reader agrees with any other either (`fread` matches base R, `vroom` is correctly rounded). Matching base R exactly is the guarantee users can check. Decision 15 is how that is done fast. |
| 6 | Invalid UTF-8 is an error (§13) | Mark as `CE_UTF8` unvalidated, or as `CE_BYTES` | Early, located failure beats a later `invalid multibyte string` deep in user code; `encoding=` in v0.2 is the escape. |
| 7 | Row limit is `INT_MAX` data rows (§15) | `R_XLEN_T_MAX` | `data.frame` row names and `nrow()` are integer; a longer object would not be a valid data frame. |
| 8 | Pass 1 validates everything; pass 2 only converts (§9) | Validate forced types lazily in pass 2 | Pass 2 never errors on content, so the only pass-2 failure is "file changed", and allocation happens once with full knowledge. |
| 9 | Cleanup via `R_UnwindProtect()` plus `R_alloc()` for all scratch memory (§16) | External pointer with finalizer; manual `free()` before each `Rf_error()` | Only two resources need manual cleanup; a finalizer would close the file at GC time rather than at error time. |
| 10 | Delimiter must be a single ASCII byte (§4) | Any single byte | Bytes >= 0x80 are never a whole character in UTF-8, so the restriction only removes inputs that could not have worked. |
| 11 | A blank record is one unquoted zero-length cell (§10) | Any one-cell zero-length record | The parser reports per-cell quoting, so `""` on its own line stays distinguishable from a bare blank line. Decision #2's documented cost is smaller than it looked when it was taken. |
| 12 | `max_columns` is set to the cap plus one (§15) | Set it to the cap | The parser truncates at `max_columns` silently, so a record reporting exactly the cap is ambiguous. One spare slot turns "at the limit" into "over the limit". |
| 13 | A zero-cell record means an oversize row (§15) | Trust a parser status code | There is no status for it — the row is dropped silently. A blank line still yields one cell, so zero cells cannot arise any other way. |
| 14 | `zucsv` installs a no-op `errprintf` (§16) | Leave `opts.errf` unset | Unset means `stderr`. Routing it to a no-op means no vendored code can write to the R session's stderr under any input, which is what CRAN's policy is about. |
| 15 | Double conversion transcribes `R_strtod5()` and self-calibrates against `R_strtod()` at first use (§11) | Call `R_strtod()` per cell (46 ns); guess R's `LDOUBLE` type from a macro | The transcription is 2.5x faster (with two exactness shortcuts: uint64 digit accumulation while the running integer is exact in the accumulator, and a power-of-ten table filled by R's own loop) and was verified bit-identical on 8M inputs, shortcuts against verbatim loops for both accumulator types, but R exposes no macro for its `LDOUBLE` choice -- a typedef guess silently broke identity on the development machine and the parity test caught it. Probing `R_strtod()` once per session picks the right instance on any build, and the per-cell call remains as the fallback if neither matches. |
| 16 | Pass 2 reuses the previous `CHARSXP` per column when the bytes match (§5) | Call `mkCharLenCE()` for every cell; a hash of recent values | Real categorical columns repeat: one `memcmp` beats hashing into R's global string cache, 24.1 → 8.0 ns/cell on KEN_ALL's prefecture column, 18% end to end on run-heavy data and ~2% cost when nothing repeats. A local hash table to catch non-adjacent repeats measured *worse* than both, so it was not built. The comparison is against the stored `CHARSXP`'s own bytes, never the previous cell's, which live in the parser's reused buffer. |
| 17 | Keep the pull API; do not adopt the SIMD engine (§18) | Push API with `scan_engine = FAST`, worth ~10% on numeric and more on text | The fast engine returns un-normalized cells, so adopting it means reimplementing CSV unquoting in `zucsv` — the one thing §28 assigns to `zsv` — and a quoted empty cell would arrive as two bytes, quietly changing §10. Prototyped and reverted; the measurements are in `tools/zsv-behavior.md`. |
| 18 | Inference skips the double grammar when the integer grammar accepts (§11) | Test all three grammars on every cell | Integer syntax is a subset of double syntax, so a cell the integer grammar takes is already known valid double and `can_double` stays true. Removes a whole digit scan per cell from an integer column, measured 0.131s → 0.119s on a 2M-cell integer file — the one shape none of the earlier conversion work helped. The subset claim is checked exhaustively over short strings and 2M random digit strings. |
| 19 | Conversion state is filled once from `R_init_zucsv()`, and calibration probes go through `zucsv_as_double()` (§11) | Lazy initialisation on first use; calibration calling the two instances directly | Lazy statics would be a data race the moment conversion moved to worker threads, and cost a branch besides. Routing the probes through the public entry point keeps each converter at exactly one call site: calling them directly let the compiler unroll the probe loop, inline a converter per probe, and spend the budget it needed for the hot path — 0.138s → 0.201s on a 2M-cell numeric file from nothing but a longer probe list. |
| 20 | `header = NA` decides from the first record alone: a header unless one field is numeric or logical syntax (§4) | `fread`/duckdb's rule — compare the first record against the types inferred from the rest | Measured against both on six shapes, the two rules agree everywhere except a header row that itself holds a numeric-looking name over a numeric column (`1,score` over `5,10`), which zucsv reads as data. Buying that case costs the two-pass design its shape: the inferred types are final only at the end of pass 1, so the verdict would have to be deferred and the first record copied out of the parser's reused buffer, and `col_types` would need a rule for validating a record that is not yet known to be data. Deciding from the first record needs neither — it happens where the record is already live, and pass 2 is untouched. The default stays `TRUE`: detection is opt-in, so no existing result changes. |
| 21 | A quoted field is text, and is skipped before either grammar, for detection (§4) | Judge the unquoted text, so `"2024"` and `2024` are the same evidence | Quoting is the one piece of authorial intent CSV carries, and `zucsv` already treats it as meaning something (decision 11: a quoted empty cell is not a blank line). A header of year or period names is an ordinary shape that the unquoted rule got wrong, and the flag is already on the cell, so the fix costs nothing. It is not free of consequence: a headerless file whose first record is entirely quoted numbers (`"1","2"`) now reads as a header, where `fread` and DuckDB read data. That shape needs a quote-everything writer *and* no header, which is rarer than a quoted header row, so the trade is taken deliberately and pinned by a test. |
| 22 | `sniff_csv()` is an early return from `read_csv()`'s own pass 1 (§4) | A separate lighter-weight implementation that samples the head of the file, as `fread` and DuckDB sniffers do | The function's only value is that it reports what the reader *would* do; an independent implementation could disagree with it, and a sampled one would disagree on exactly the awkward files someone reaches for a sniffer to understand. Sharing the pass makes agreement structural rather than tested — the tests check it anyway, over twelve fixture shapes. The cost is that sniffing is not cheap: it reads the whole file. That is the honest price of an exact `nrow` and a `col_types` that is right about the last row as well as the first. |
