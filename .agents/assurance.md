# Assurance

How this package meets the shared `assurance.md` rules. CI checks this
file (r-actions `assurance.yml`), so keep the headings and table
columns as they are.

- **Statuses:** a cell is a fact, `gap #<issue>` or `n/a: <reason>`.
- **Kinds:** delete the ones that do not apply.
- **Tables:** delete a table only when no kind requires it. Delete the
  example rows.

## Kinds

`compiled` `untrusted-input` `spec` `lifecycle`

zucsv compiles a C reader and a vendored parser (src/), parses CSV bytes it
did not produce, and implements RFC 4180 CSV. Nothing read_csv() returns
outlives the call, and the file handle and parser it opens are released on
every path by R_UnwindProtect (design §16); it is lifecycle for the C API's
stream (design §30, #23), which another package holds across calls. It is
not emits: it writes nothing. (No backticks here: the check reads each as a
declared kind.)

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
| fuzz          | done   | fuzz.yaml          |
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
| the Arrow C stream (#23), values            | `read_csv()` on the same file and options: identical, doubles bit for bit in every converter mode, every batch size | tests/testthat/test-stream.R, tools/zucsvlink/tests/testthat/test-consume.R |
| the Arrow C stream, errors                  | `read_csv()`'s message for the same input, word for word                                           | tests/testthat/test-stream.R                                       |
| the Arrow C stream, layout                  | the bytes the other side received: zuarrow's full validation (`arrow_to_r()`), and the fixture's own reader | tests/testthat/test-stream.R, tools/zucsvlink/tests/testthat/test-consume.R |
| the stream on threads that are not R's      | ThreadSanitizer, with a negative control it must report                                            | consumer.yaml (own), tools/capi/test_capi.c                         |
| pass 2 against pass 1, any bytes            | the counts pass 1 found, and a second read and a read in batches of three                          | fuzz.yaml, tools/capi/fuzz.c                                        |
| the static archive and the shared object    | their symbol tables: no R, no forbidden call, no mutable global, no zuarrow object, one export     | tests/testthat/test-abi.R                                          |
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
| stream batch, from 1 row                 | rows in a batch                                         | tests/testthat/test-stream.R (0 refused; 1, 3, 9, 10, 11 over 10 rows); tools/capi/test_capi.c (1, 7, 4096, 20000, 20001 over 20,000) |
<!-- markdownlint-enable MD013 -->

## Error branches R cannot reach

From #34. Every other failure in `src/core.c` (since #23 the checks live
there and `read_csv()` raises their messages) and every `Rf_error()` in
`src/read_csv.c` has a test that reaches it, by `read_csv()`/`sniff_csv()`
or, for the C argument checks the wrappers shadow, by `.Call`
(tests/testthat/test-arguments.R).

| Branch                                                        | Why a test cannot reach it                                                                          |
| ------------------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| "Ran out of memory while parsing" (zsv status)                | needs `malloc` to fail inside the vendored parser; the alloc-failure check's job (#50)         |
| "Ran out of memory converting a numeric cell"                 | needs `malloc` to fail in the `R_strtod` fallback; the same                                          |
| "CSV parser failed while reading" (zsv status)                | no input makes the pull parser return an error status; recorded in tools/zsv-behavior.md             |
| "Cannot create CSV parser"                                    | `zsv_new()` fails only when allocation does                                                          |
| "CSV file changed while it was being read", by `read_csv()`   | needs the file rewritten between the passes; a test would race the reader. The stream reaches it: tests/testthat/test-stream.R and tools/capi rewrite the file after open |
| "CSV text parsed differently on the second pass"              | a string cannot change between the passes; the check is a backstop                                   |
| "CSV has more than 2147483647 rows"                           | see the rows limit above                                                                            |
| "Ran out of memory" in `src/core.c` and `src/stream.c` (names, columns, na copies, batches, schema) | needs `malloc` to fail; the alloc-failure check's job (#50)                                  |
| "strtod_mode %d is not usable on this machine"                | needs a machine where a fast instance's assumptions fail (valgrind's long double); the probe never returns such a mode, so only a consumer passing one by hand reaches it |

## Objects

One row per object that outlives the call that made it. "Sequences" is
the test of its out-of-order use. The guard columns name the test, or say
why the guard cannot apply.

<!-- markdownlint-disable MD013 -->
| Object | Sequences | Fork | Unwind | Owner |
| ------ | --------- | ---- | ------ | ----- |
| C API stream (`zucsv_open_stream()`, #23), and the error stream a failed open leaves | tools/capi/test_capi.c: a failed open's `get_schema()` and `get_next()` repeat its code and message; `get_next()` after the end; a file changed after open fails and stays failed; tests/testthat/test-stream.R the same through R; tools/capi/fuzz.c checks the failure and end paths on every input | n/a: it holds a `FILE *` between open and the end, which a forked child would share; the consumer guards its own objects (zugodb records each struct's process), and zucsv has no fork-time hook in C | n/a: no R on its paths, nothing long-jumps over it; read_csv()'s own use of the core is released by R_UnwindProtect (design §16) | its caller releases it, exactly once; batches own their memory and outlive the stream; the test hooks release it inside R_UnwindProtect (src/r_stream.c) |
<!-- markdownlint-enable MD013 -->
