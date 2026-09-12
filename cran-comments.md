## R CMD check results

0 errors | 0 warnings | 1 note

* This is a new submission.

## Notes for the reviewer

* The package bundles a subset of the 'zsv' CSV parser
  (<https://github.com/liquidaty/zsv>), which is MIT licensed. Its copyright
  holder is listed in `Authors@R` with role `cph`, the affected files and
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

## Test environments

* local macOS 15, R 4.5.2
* GitHub Actions: macOS, Windows, Ubuntu (R-devel, release, oldrel-1)
* GitHub Actions: rocker/r-devel-san (ASan/UBSan)
