## Resubmission

This is a resubmission. In response to the review of 2 October 2026:

* `Authors@R` now names every author and copyright holder of the bundled
  code, taken from the copyright notices in the upstream file headers:
  Tai Chi Minh Ralph Eastwood (`ctb`, `cph`) and Matt Wong (`ctb`) as authors
  of the 'zsv' library; Guarnerix Inc dba Liquidaty (`cph`), which holds the
  copyright in Matt Wong's contribution and in the files without a per-file
  notice; "zsv contributors" (`ctb`, `cph`) for the files whose notice reads
  "Liquidaty and the zsv/lib contributors"; and Max Ogden (`ctb`, `cph`), the
  author of the 'csv-spectrum' fixtures transcribed into the tests.
* `inst/COPYRIGHTS` now reproduces each bundled file's copyright notice and
  maps it to these entries.
* No code has changed.

## R CMD check results

0 errors | 0 warnings | 1 note

* This is a new submission.
* "LF" in the Description, flagged as possibly misspelled, is the line-feed
  line ending (as in "LF and CRLF line endings").

## Notes for the reviewer

* The package bundles a subset of the 'zsv' CSV parser
  (<https://github.com/liquidaty/zsv>), which is MIT licensed. Its authors
  and copyright holders are listed in `Authors@R`, the affected files and
  their licence are itemised in `inst/COPYRIGHTS`, and the exact upstream
  commit and the patches applied to it are recorded in
  `src/vendor/zsv/UPSTREAM`. Only the parser library is included; the zsv
  command-line application and its SQL, SQLite, JSON and extension facilities
  are not part of this package.

* The compiled code has been checked under AddressSanitizer and
  UndefinedBehaviorSanitizer, and holds no reference to stdout or stderr:
  the parser's default diagnostic sink is patched to a no-op so that no
  bundled code can write to the console.

* Tests that need large inputs (a table at the 65,536 column limit, 200,000
  rows, a multi-megabyte cell) are behind `skip_on_cran()`. Everything runs
  in `tempdir()`.

## Method references

There are no published references describing the methods in this package. It
implements CSV parsing as defined by RFC 4180, and column-type inference whose
rules are documented in `?read_csv` and chosen to agree with
`utils::read.csv()` on the values both accept.

## Test environments

* local macOS 26.6.2, R 4.6.1
* GitHub Actions: macOS, Windows and Ubuntu on R release; Ubuntu on oldrel-1
* GitHub Actions, in the R-hub containers built to match CRAN's r-devel Linux
  flavors -- clang23, ubuntu-clang and ubuntu-gcc16 -- compiled as CRAN
  compiles them, with `CC += -std=gnu23` and `CFLAGS += -pedantic`
* GitHub Actions (pedrobtz/r-actions@v1): AddressSanitizer and
  UndefinedBehaviorSanitizer under R-devel, valgrind, LTO, gctorture, and
  rchk
