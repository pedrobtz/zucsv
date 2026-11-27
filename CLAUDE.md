# CLAUDE.md

<!-- markdownlint-disable-next-line MD013 -->
This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Shared conventions

<!-- r-skills: shared-rules -->

The portfolio's shared rules (`workflow`, `r-package`, `ci`, `native-code`,
`correctness`, `assurance`) come from the
[r-skills](https://github.com/pedrobtz/r-skills) plugin's `rules/`, loaded at
session start because of the marker above. Never copy them here. Where this
file and a shared rule disagree, this file wins for this package. Cloud
sessions do not load the plugin and run without them.

## What this is

`zucsv` reads CSV files into data frames. `read_csv()` in
[R/read_csv.R](R/read_csv.R) is a thin wrapper over the C reader: the
R-free core in `src/core.c` and `src/convert.c`, built on the vendored
parser under `src/vendor/zsv/`, and `src/read_csv.c`, which turns the core's
rows into R vectors. Base R C API only, no runtime R package dependencies.

Since #23 the same core is also a C API for other packages (design §30):
`zucsv_open_stream()` in `inst/include/zucsv.h` reads a file as an Arrow C
stream (`src/stream.c`), linked from `libzucsv.a`, which the package
installs in `lib/`. Its arrays are built with zuarrow's bundled nanoarrow,
so zuarrow is a build-time dependency (`LinkingTo`, pinned by `Remotes:`).

The full design document is
[.agents/zucsv-design.md](.agents/zucsv-design.md) and the staged plan is
[.agents/ROADMAP.md](.agents/ROADMAP.md). Both are build-ignored
(`^\.agents$`), so they stay out of the tarball. They outrank this file.

**Read `.agents/zucsv-design.md` before writing any code.** It is the
specification for v0.1 and covers the public API, type-inference rules, error
messages, two-pass strategy, resource limits, the `src/` file layout, and the
explicit non-goals. Treat its "Non-goals for v0.1" (§3) as binding: do not add
`write_csv()`, connections, URL input, date parsing, column selection,
`skip`/`n_max`, ALTREP, or a parser-mode switch unless the user asks to revise
the design. They still bind after 0.1.0, until a design revision lifts one.
§29 is the decision log; check it before reopening a choice that looks
arbitrary. `.agents/ROADMAP.md` says which stage the work is in and what its
exit criteria are; finish a stage's criteria before starting the next.

## Current state

0.1.0 is on CRAN, published 2026-10-16 from commit `1cc69d9` (the
resubmission that named every `zsv` author in `Authors@R`). `main` is at
`0.1.1`, the release commit, green on the full set and ready to submit
from 2026-11-27 (#14). Until the `.9000` bump after submission, a
user-visible change on `main` changes what is submitted, so hold it for
0.1.2 or add it under `# zucsv 0.1.1` deliberately. The `0.1.0` tag (on `1cc69d9`)
and its GitHub release were created by hand on 2026-11-19. From 0.1.1,
`cran-release.yaml` tags and releases each submission when
`CRAN-SUBMISSION` is committed. Tags are the bare version, `0.1.0`, not
`v0.1.0`.

## Versions and dependents

The next release is v0.1.1, tracked in
[#13](https://github.com/pedrobtz/zucsv/issues/13): attach planned work to it
as sub-issues. CRAN's cadence opens on 2026-11-27, six weeks after publication
([#14](https://github.com/pedrobtz/zucsv/issues/14)); that is the earliest a
submission is allowed, not a plan to submit. Sibling packages that depend on
this one: `zumime` (on CRAN, `Suggests: zucsv`; it calls `sniff_csv()` on
arbitrary files inside `tryCatch()`, so a new error for malformed input
leaves its result unchanged). Check it against every release. `zugodb`
links the C API (`LinkingTo: zucsv`, its Stage 10, pedrobtz/zugodb#60):
a change to `zucsv.h`, `zucsv-r.h` or what the stream delivers is a change
to its contract, and an incompatible one bumps `ZUCSV_ABI_VERSION`.

Any new vendored file that names an author means a new `Authors@R` entry:
CRAN asked for every one by name.

## Commands

Run from the package root. The everyday `devtools` loop is in
the shared `r-package.md` rules; these are the package's own.

```sh
R CMD INSTALL --preclean .              # when object files go stale
R CMD build . && R CMD check --as-cran zucsv_*.tar.gz
ZUCSV_STRESS=true Rscript -e 'devtools::test()'   # stress tests where NOT_CRAN is unset
Rscript bench/bench.R [reps]            # two-pass overhead vs read.csv()
tools/update-zsv.sh <commit-ish>        # re-vendor zsv, re-apply patches
bash tools/ci/consumer.sh               # the consumer fixture, tools/zucsvlink
make -C tools/capi ZUARROW=<zuarrow checkout> [tsan|tsan-negative|asan|replay] CC=clang
Rscript tools/update-csv-spectrum.R     # regenerate test-spectrum.R
Rscript tools/check-csv-test-data.R     # csv-test-data conformance check
```

After editing C sources, `devtools::load_all()` recompiles; use
`R CMD INSTALL --preclean .` when object files go stale, and always after
changing `src/Makevars.in`: `src/Makevars` and `src/Makevars.win` are
written from it by `./configure` (which finds zuarrow's `libzuarrow.a`) and
are not in git, and make does not rebuild objects when they change.

## Architecture

The design splits responsibility at one line (design §28):

```text
zsv owns CSV syntax        →  where rows and fields begin and end
zucsv owns R semantics     →  NA matching, column types, data.frame
                              construction, error messages
```

Consequences that shape most implementation decisions:

- **The core is R-free** (`src/core.c`, `src/convert.c`, `src/stream.c`):
  no R header, no R call, no mutable global, since they are what
  `libzucsv.a` holds and run on consumers' threads. A check returns an
  errno code and leaves `read_csv()`'s message in the core; `read_csv()`
  raises it. `tests/testthat/test-abi.R` and `tools/capi` (which builds
  them with no R on the include path) enforce it.
- **Base R C API only, via `.Call`.** No Rcpp, no cpp11, no runtime R package
  dependencies. Native symbols get registered in `src/init.c` with dynamic
  symbol lookup disabled; `R/zucsv-package.R` carries the
  `@useDynLib zucsv, .registration = TRUE` tag.
- **Thin R wrapper.** `read_csv()` in `R/read_csv.R` normalizes arguments and
  calls one C routine.
- **Two passes over the file** (design §9): pass 1 counts rows, fixes the
  column count, applies `na` matching, and infers types; pass 2 allocates
  each column at its exact final length and writes converted cells straight
  into it. No growable vectors, no intermediate row lists. This is why v0.1
  accepts only seekable local file paths.
- **`zsv` is vendored**, parser subset only, under `src/vendor/zsv/` (see
  Vendored native code).

Exported names are unprefixed (`zucsv::read_csv()`, not `zu_read_csv()`):
the package name is the namespace.

## Invariants that are easy to break

- **Validation that affects native safety is enforced in C**, not only in
  the R wrapper.
- **Strict errors, never silent `NA`.** A failed forced conversion, a ragged
  row, or an embedded NUL is an error carrying row, column, and column-name
  context. Raw `zsv` status codes stay internal.
- **Long jumps are the hazard.** Every `FILE *`, `zsv_parser`, and native
  allocation hangs off the `zucsv_core`, which the `R_UnwindProtect()`
  cleanup closes on R errors as well as on success, and the core's progress
  hook, which `read_csv()` points at `R_CheckUserInterrupt()`, runs
  periodically (about every 16k rows), not per row (design §16).
- **The double converter's mode is never found lazily.** It is probed once
  by `zucsv_strtod_probe()` (`inst/include/zucsv-r.h`) from `R_init_zucsv`,
  as every consumer does, and passed to each read (design decision 25).
- **`libzucsv.a` holds no zuarrow object**: a consumer links it and then
  `libzuarrow.a`. The zuarrow commit is pinned in four places that must
  agree: DESCRIPTION's `Remotes:`, and `consumer.yaml`, `fuzz.yaml` and the
  rchk job in `native-checks.yaml`.
- **Never build against a system `zsv`.**
- **R-specific adaptation belongs in `zucsv`'s own `src/` files**, never in
  `src/vendor/`. The only changes to upstream are the patches in
  `tools/patches/`, applied by `tools/update-zsv.sh`.

## Testing

- **Fixtures are raw bytes.** `helper-fixtures.R` writes them with
  `csv_file()` and `read_text()`, so line endings, BOMs, NULs and invalid
  UTF-8 are exact and the test files stay pure ASCII.
- **`test-spectrum.R` is generated** from csv-spectrum (BSD-2-Clause) by
  `tools/update-csv-spectrum.R`, as raw byte vectors so git cannot normalise
  the CRLF fixtures. Regenerate it; do not edit it by hand.
- **csv-test-data is not in the suite**: it declares no licence, so
  `tools/check-csv-test-data.R` clones it at run time. Every fixture has a
  recorded expectation there, including the ones where zucsv deliberately
  differs, each with the design section that decides it.
- **`test-stream.R` holds the Arrow stream to `read_csv()`** (identical,
  doubles bit for bit, every error's message), and `test-abi.R` the archive
  and the shared object to their symbol tables (helpers from zuarrow's).
  `same_bits()` compares NaN bits too (`single.NA = FALSE`).
- **`test-upstream.R` pins the vendored parser's edge behaviour** recorded
  in `tools/zsv-behavior.md`; it is what catches a change on re-vendoring.
- **`test-differential.R` compares with `utils::read.csv()`** where the
  semantics overlap, with name repair off (zucsv does not repair names,
  design §5).
- **Heavy tests skip under gctorture, detected, not opted out of.**
  `skip_under_gctorture()` reads `gctorture2()`'s step, which r-actions sets
  in-process. `skip_unless_stress()` also skips on CRAN, for memory, and
  `ZUCSV_STRESS=true` forces it on. Every other leg runs the stress tests,
  2-3 s in all (#33). They check limits, not memory safety, and under
  gctorture they would cost hours.

## Continuous integration

The callers follow r-actions' `examples/`. Pull requests run the quick
profile (`profile: auto`); the schedule, a dispatch and the `full-ci` label
run the full set. Every caller cancels a superseded pull request run, reruns
when a CI label is added or removed, and skips its jobs without a runner
under `ci:docs`, `ci:minimal` and `skip-ci:<workflow>`. The full set runs
weekly in zucsv's Saturday slot: R-CMD-check at 20:04 UTC, coverage at
20:09, native-checks at 20:14 and gctorture at 20:19. Run it on demand with
`gh workflow run <file> --ref main`.

- `R-CMD-check.yaml` calls `r-cmd-check.yml@v1`: the GitHub runners plus the
  R-hub containers matching CRAN's r-devel clang and GCC flavours, which see
  the bundled parser's warnings before CRAN's pretest does. A pull request
  runs Ubuntu R-release and the clang 23 container.
- `native-checks.yaml` calls r-actions' `vendor.yml` (a change under
  `src/vendor` must also change `src/vendor/zsv/UPSTREAM`, the manifest),
  `sanitizers.yml` (UBSan on pull requests, plus `asan: true` in full),
  `valgrind.yml` (full only, with `--leak-check=full`, so leaks on error and
  interrupt paths carry a stack), `lto.yml`, `rchk.yml`
  (`fail-on-findings: true`) and `cran-special.yml` (full only).
- `gctorture.yaml` runs the quick step under a 10-minute budget on pull
  requests, and step 20 on the schedule and on dispatch. A failed scheduled
  run opens, or comments on, an issue labelled `gctorture`. Dispatch it on
  the branch before merging a change to `PROTECT`s or the CHARSXP cache, and
  set its `cron` to daily while a release changes `src/`.
- `assurance.yaml` calls `assurance.yml@v1` on every pull request, holding
  `.agents/assurance.md` to the standard; each gap there names an issue
  labelled `assurance`.
- `cran-release.yaml` calls `cran-release.yml`, pinned to a commit because
  it holds a write token. It runs when a commit to `main` changes
  `CRAN-SUBMISSION`: it tags the submitted commit with the bare version,
  creates the GitHub release from `NEWS.md`, and opens the `.9000` bump
  pull request, to merge only after CRAN accepts. `CRAN-SUBMISSION` is
  committed after every `devtools::submit_cran()`; it is in `.Rbuildignore`,
  never `.gitignore`.
- `coverage.yaml` calls `coverage.yml`, pinned to a commit because it holds a
  write token; the badge goes to `gh-pages`. `pkgdown.yaml` deploys the site
  on pushes to `main` and on releases.

- `consumer.yaml` (#23) is the package's own workflow, gated by
  `changes.yml`: the consumer fixture (`tools/ci/consumer.sh`) on Linux,
  and on macOS and Windows in the full profile, and the R-free C suite
  (`tools/capi`) under ASan, UBSan and ThreadSanitizer with its negative
  control. `fuzz.yaml` calls r-actions' `fuzz.yml` with
  `tools/capi/fuzz.c`: two minutes on a pull request, an hour on the
  schedule.

**Deviation from the shared `ci.md` rules:** only `consumer.yaml` calls
`changes.yml`. Every other job calls an r-actions workflow, which
classifies a docs-only change itself. There is no `analyzers` job yet.

## Vendored native code

`zsv` from <https://github.com/liquidaty/zsv>, MIT, parser core only, at
commit `61959542b18581e38f291957d9a500fab77be719` (`v1.4.3-41-g6195954`),
vendored 2026-09-12. `src/vendor/zsv/UPSTREAM` records the upstream, commit,
date, licence and the patches; `tools/update-zsv.sh <commit-ish>` re-vendors
and rewrites it. `src/vendor/zsv/src/zsv.c` is a unity build, so
`src/Makevars` lists `vendor/zsv/src/zsv.o` as the only vendored object.
`ZSV_EXTRAS` is never defined, which keeps `<sqlite3.h>` and
`<zsv/utils/arg.h>` out of the build.

Local patches, in `tools/patches/`:

- `0001-guard-unused-forward-declaration.patch`: keeps `-Wunused-function`
  clean.
- `0002-no-stderr-default-sink.patch`: removes the `stderr`/`fprintf`
  references so R CMD check's compiled-code test passes.
- `0003-report-unclosed-quote-at-eof.patch`: reports a quote still open at
  end of input through `errprintf`, which zucsv turns into an error (#30).
- `0004-unescape-pending-quote-at-eof.patch`: keeps the closing quote out of
  an escaped last cell when the input has no final newline (#45).
- `0005-vector-scan-mask-base.patch`: places the vector scan's tokens
  relative to the vector that holds them, which removes a quadratic rescan
  (#31).
- `0006-overridable-export-visibility.patch`: lets `-DZSV_EXPORT=` give
  zsv's functions the build's hidden visibility, so `libzucsv.a` exports
  nothing from a consumer's shared object (#23).

0003 to 0005 fix upstream behaviour and are to be reported to
`liquidaty/zsv`. Drop each one when upstream fixes it. zucsv also relies on
the exact text of two `errprintf` formats ("Warning: row %zu truncated" and
0003's), so check them after any upgrade.

After an upgrade, re-verify `tools/zsv-behavior.md` (what the parser does at
the edges the design depends on: blank records, BOM, oversize rows,
unbalanced quotes); `tools/update-zsv.sh` does not rewrite it.
