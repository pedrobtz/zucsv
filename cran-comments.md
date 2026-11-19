## Update

This is an update from 0.1.0 to 0.1.1, a patch release.

It fixes inputs that read as a wrong table with no error. A record longer
than the size limit, when the overflow fell after its first cell, read as a
short row plus an invented one. A quote never closed in the last column took
the rest of the file into one cell. An escaped last cell kept its closing
quote when the file had no final newline. Non-ASCII `text =` input was altered
in a C locale. It also removes quadratic time on long cells. See `NEWS.md`.

Three of the fixes are in the bundled 'zsv' parser. They are applied as
recorded patches (`tools/patches/0003` to `0005`, listed in
`src/vendor/zsv/UPSTREAM`), and no new bundled file or author is added.

## R CMD check results

0 errors | 0 warnings | 0 notes

## Reverse dependencies

One package on CRAN, 'zumime', suggests 'zucsv'. Its R CMD check passes with
this version installed (0 errors, 0 warnings). Its use of `sniff_csv()`
handles the new errors for malformed input as it handles the existing ones.

## Test environments

* local macOS 15.7, R 4.6.2
* GitHub Actions (pedrobtz/r-actions@v1): macOS, Windows and Ubuntu on R
  release; Ubuntu on oldrel-1
* GitHub Actions, in the R-hub containers built to match CRAN's r-devel Linux
  flavors -- clang23, ubuntu-clang and ubuntu-gcc16 -- compiled as CRAN
  compiles them, with `CC += -std=gnu23` and `CFLAGS += -pedantic`
* GitHub Actions (pedrobtz/r-actions@v1): AddressSanitizer and
  UndefinedBehaviorSanitizer under R-devel, valgrind, LTO, gctorture, rchk,
  and CRAN's rcnst, rlibro and vnu checks
