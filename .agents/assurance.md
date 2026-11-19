# Assurance

How this package meets the shared `assurance.md` rules. CI checks this
file (r-actions `assurance.yml`), so keep the headings and table
columns as they are.

- **Statuses:** a cell is a fact, `gap #<issue>` or `n/a: <reason>`.
- **Kinds:** delete the ones that do not apply.
- **Tables:** delete a table only when no kind requires it. Delete the
  example rows.

## Kinds

`compiled` `untrusted-input` `spec`

zucsv compiles a C reader and a vendored parser (src/), parses CSV bytes it
did not produce, and implements RFC 4180 CSV. It is not lifecycle: nothing it
returns outlives the call, and the file handle and parser it opens are released
on every path by R_UnwindProtect (design §16). It is not emits: it writes
nothing. (No backticks here: the check reads each as a declared kind.)

## Required checks

`Where` names the workflow file under `.github/workflows/` that runs the
check. Add `(own)` when that workflow is the package's own rather than a
call to r-actions.

| Check         | Status | Where              |
| ------------- | ------ | ------------------ |
| sanitizers    | done   | native-checks.yaml |
| valgrind      | done   | native-checks.yaml |
| rchk          | done   | native-checks.yaml |
| gctorture     | done   | gctorture.yaml     |
| analyzers     | gap #49 |                    |
| alloc-failure | gap #50 |                    |
| mutation      | gap #51 |                    |
| fuzz          | gap #52 |                    |
| fuzz-reach    | gap #53 |                    |

## Boundaries

One row per boundary the package crosses. An oracle knows the answer:

- a conformance suite or test vectors;
- a reference implementation;
- a round trip;
- equivalent inputs compared;
- the bytes the other side received.

"No crash" is not an oracle.

<!-- markdownlint-disable MD013 -->
| Boundary                                   | Oracle                                                                                              | Carried by                                                         |
| ------------------------------------------ | --------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| CSV syntax (RFC 4180), decoding            | csv-spectrum conformance suite (11 of 12 fixtures, raw bytes)                                       | tests/testthat/test-spectrum.R                                     |
| CSV syntax, decoding                       | `utils::read.csv()` as reference, and a `write.csv()` round trip                                    | tests/testthat/test-differential.R                                 |
| CSV syntax, decoding                       | csv-test-data corpus, every fixture with a recorded expectation                                     | gap #54                                                             |
| CSV syntax at end of input and long runs   | `utils::read.csv()` on the terminated form; every vector width                                      | tests/testthat/test-upstream.R, tests/testthat/test-scan.R         |
| file and `text =` routes                   | equivalent inputs compared: the same bytes by both routes, and `sniff_csv()` against `read_csv()`   | tests/testthat/test-structure.R, tests/testthat/test-record-limit.R |
| decimal text to double                     | bit-identical to `as.numeric()`; each fast instance against R's verbatim loops                      | tests/testthat/test-strtod.R, tests/testthat/test-types.R          |
| integer and logical grammar                | `utils::read.csv()` type inference on the same input                                                | tests/testthat/test-differential.R                                 |
| UTF-8 validation of cells and names        | hand fixtures only                                                                                  | gap #55                                                             |
| `text =` encoding translation to UTF-8     | one latin1 case, hand value                                                                         | gap #56                                                             |
<!-- markdownlint-enable MD013 -->

## Limits

One row per limit the package enforces. "Straddled by" is the test of
`limit - 1`, `limit` and `limit + 1` on every path that reaches it.

<!-- markdownlint-disable MD013 -->
| Limit                                    | Unit                                                    | Straddled by                                                                                                     |
| ---------------------------------------- | ------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| columns, 65,536                          | cells in a record                                       | tests/testthat/test-errors.R (65535, 65536, 65537; file, text, `sniff_csv()`; a data row over a header at it)     |
| record size, 8,388,607                   | bytes of a record: delimiters and quotes, no terminator | tests/testthat/test-record-limit.R (L-1, L, L+1 on LF, CRLF and none; file and text; every overflow position)    |
| error-message value, 40                  | bytes of the offending cell shown                       | tests/testthat/test-col-types.R (39, 40, 41)                                                                     |
| integer range, ±2,147,483,647            | value of an integer cell                                | tests/testthat/test-types.R (at the limit and one past it, both signs; `-2147483648` is `NA_integer_`)           |
| rows, 2,147,483,647                      | records                                                 | n/a: unreachable in a test, since the smallest input over it is 2^31 records of at least 2 bytes, over 4 GiB      |
| interrupt check, 16,384 records or 64 MiB | records, and bytes scanned                             | gap #46                                                                                                          |
<!-- markdownlint-enable MD013 -->

## Error branches R cannot reach

From #34. Every other `Rf_error()` in `src/read_csv.c` and `src/convert.c` has
a test that reaches it, by `read_csv()`/`sniff_csv()` or, for the C argument
checks the wrappers shadow, by `.Call` (tests/testthat/test-arguments.R).

| Branch                                                        | Why a test cannot reach it                                                                          |
| ------------------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| "Ran out of memory while parsing" (zsv status)                | needs `malloc` to fail inside the vendored parser; the alloc-failure check's job (#50)         |
| "Ran out of memory converting a numeric cell"                 | needs `malloc` to fail in the `R_strtod` fallback; the same                                          |
| "CSV parser failed while reading" (zsv status)                | no input makes the pull parser return an error status; recorded in tools/zsv-behavior.md             |
| "Cannot create CSV parser"                                    | `zsv_new()` fails only when allocation does                                                          |
| "CSV file changed while it was being read", pass-2 bounds     | needs the file rewritten between the passes; a test would race the reader                            |
| "CSV text parsed differently on the second pass"              | a string cannot change between the passes; the check is a backstop                                   |
| "CSV has more than 2147483647 rows"                           | see the rows limit above                                                                            |
