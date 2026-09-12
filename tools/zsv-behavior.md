# Verified upstream `zsv` behavior

What the vendored parser actually does at the edges `zucsv`'s design depends
on. Established empirically against the commit recorded in
`src/vendor/zsv/UPSTREAM` by compiling the vendored sources and driving the
pull API directly.

This file survives re-vendoring (`tools/update-zsv.sh` rewrites `UPSTREAM`,
not this file). **Re-verify it on every upgrade** — each row below is a
behavior `zucsv` relies on, and `tests/testthat/test-upstream.R` is what
catches a change.

Legend: `n` = `zsv_cell_count()`, `len` = `zsv_cell.len`,
`q` = `zsv_cell.quoted`.

## Parser driving

`zsv_next_row()` pulls its own input through `opts.read` (default `fread`)
from `opts.stream`. It must **not** be mixed with `zsv_parse_bytes()` or
`zsv_parse_more()`: pushing bytes in and then calling `zsv_next_row()`
yields zero rows. `zucsv` therefore sets `opts.stream` to its `FILE *` and
uses the default read function.

`zsv_next_row()` returns `zsv_status_row` (6) per record and
`zsv_status_done` (100) at end of input — not `zsv_status_ok` (0), despite
what the header comment on the function says.

## Records and blank lines

| Input | Result |
|---|---|
| `a,b\n1,2\n` | 2 records, n=2 each |
| `a,b\n1,2` (no final newline) | 2 records; the last record is still emitted |
| `a,b\r\n1,2\r\n` | 2 records, n=2; CRLF handled |
| `a,b\n1,2\n\n` (trailing blank) | **3 records**; the blank is `n=1, len=0, q=0` |
| `a,b\n\n1,2\n` (interior blank) | **3 records**; the blank is `n=1, len=0, q=0` |
| `a,b\n\n\n1,2\n` | **4 records**; one blank record each |
| `\na,b\n1,2\n` (leading blank) | **2 records** — the leading blank is swallowed |
| `\n\n` (blank-only file) | 0 records |
| `` (empty file) | 0 records |
| `a,b\n` (header only) | 1 record |

Blank lines are *not* skipped by the parser: they arrive as one-cell records
with an empty value. `zucsv` implements the §10 skip policy itself by
dropping any record with `n == 1 && len == 0 && !(q & ZSV_PARSER_QUOTE_CLOSED)`.

The leading-blank asymmetry comes from `opts.keep_empty_header_rows`, which
defaults to 0 ("zsv ignores empty header rows"). It does not matter to
`zucsv`, whose policy discards those records anyway, but it does mean the
parser's record numbering can differ from a byte-offset line count. `zucsv`
numbers records as the parser emits them.

## A quoted empty cell is distinguishable from a blank line

This is what makes the §10 escape hatch real:

| Input | Record 2 |
|---|---|
| `a\n\nb\n` (bare blank line) | `n=1, len=0, q=0` |
| `a\n""\nb\n` (quoted empty) | `n=1, len=0, q=2` |

`q=2` is `ZSV_PARSER_QUOTE_CLOSED` ("value was quoted"). So in a
single-column file an intentional empty cell written as `""` survives, while
a bare empty line is skipped. The same flag distinguishes `,` from `"",""`
in multi-column input.

## BOM

A UTF-8 BOM is stripped by the parser: `\xEF\xBB\xBFa,b\n1,2\n` yields a
first cell of exactly `a`. A file containing only a BOM yields 0 records.
`zucsv` needs no BOM handling of its own, but §13 still guarantees the
behavior, so it stays covered by a fixture.

## Quoting

| Input cell | Parsed value |
|---|---|
| `"x,y"` | `x,y` — delimiter preserved |
| `"he said ""hi"""` | `he said "hi"` — unescaped |
| `"x\ny"` | `x\ny` — embedded LF preserved |
| `"x\r\ny"` | `x\r\ny` — embedded CRLF preserved byte for byte |
| `"x\ry"` | `x\ry` — a lone CR is preserved too |
| `x"y` (quote mid-cell) | `x"y` — accepted, passed through |
| `"unterminated,2\n` | one cell, `unterminated,2\n` — no error status |

Bytes inside a quoted field are passed through untouched, CR included; only
the *record* terminator is consumed. `zsv_status_nonstandard_csv` is raised
only by the
fast/SIMD engine, which `zucsv` does not use (§18), so malformed quoting is
never an error from the parser — it surfaces as a row-width mismatch.

## Silent truncation: two paths `zucsv` must detect

Neither raises an error status. Both would otherwise lose data silently.

**Oversize row → `n == 0`.** A row larger than the internal buffer is
emitted as a record with *zero* cells:

| Input | Result |
|---|---|
| 500 KB row, default `buffsize` | record with `n=0` |
| 500 KB row, `buffsize` = 2 MB | record with `n=2`, 500001 bytes, intact |
| 500 KB row, `max_row_size` = 1 MB | record with `n=2`, 500001 bytes, intact |

`n == 0` is an unambiguous overflow signal: even a blank line yields `n == 1`.
`zucsv` sets a generous `max_row_size` and treats `n == 0` as the §15
record-size error.

**Too many columns → silent truncation at `max_columns`.** A 20-column row
parsed with `max_columns = 19` yields `n = 19`, with no error and no way to
tell it from a genuine 19-column row. `max_columns = 0` means the upstream
default of 1024, not unlimited, so `zucsv` must always set it explicitly.

`zucsv` therefore sets `max_columns` to its cap **plus one** (65537) and
errors when `n > 65536`. That distinguishes a file at the cap from one over
it.

## Embedded NUL and malformed UTF-8

A NUL inside a cell is passed through in the cell data with the length
counting it, so `zucsv` must check `len`, never treat `str` as a C string.

Malformed UTF-8 is passed through untouched by default
(`opts.malformed_utf8_replace == 0`). `zucsv` validates it itself (§13).
Note for a later version: `opts.malformed_utf8_handler` is a per-cell
callback that would do the same job inside the parser's existing scan.

## stderr

The parser writes to `stderr` only through `opts.errprintf` with `opts.errf`,
and only on an invalid delimiter — which `zucsv` rejects before calling
`zsv_new()`. `zucsv` still installs a no-op `errprintf`, so that no vendored
code can write to the R session's stderr under any input. This is what keeps
the two `stderr` references in the vendored sources harmless for CRAN.
