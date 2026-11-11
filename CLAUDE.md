# CLAUDE.md

<!-- markdownlint-disable-next-line MD013 -->
This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Shared conventions

The portfolio's shared rules are synced into `.claude/shared/` from
`pedrobtz/packages:claude/shared/`. Never edit them here. Where this file and
a shared file disagree, this file wins for this package.

@.claude/shared/workflow.md
@.claude/shared/r-package.md
@.claude/shared/ci.md
@.claude/shared/native-code.md
@.claude/shared/correctness.md
@.claude/shared/assurance.md

## What this is

`zucsv` reads CSV files into data frames. `read_csv()` in
[R/read_csv.R](R/read_csv.R) is a thin wrapper over the C reader in
`src/read_csv.c` and `src/convert.c`, built on the vendored parser under
`src/vendor/zsv/`. Base R C API only, no runtime R package dependencies.

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
`0.1.0.9000`, with a `# zucsv (development version)` heading in `NEWS.md`;
record every user-visible change under it. As of 2026-11-11 the `0.1.0` tag
and its GitHub release have still not been created. The tag is the bare
version, `0.1.0`, not `v0.1.0`.

## Versions and dependents

The next release is v0.1.1, tracked in
[#13](https://github.com/pedrobtz/zucsv/issues/13): attach planned work to it
as sub-issues. CRAN's cadence opens on 2026-11-27, six weeks after publication
([#14](https://github.com/pedrobtz/zucsv/issues/14)); that is the earliest a
submission is allowed, not a plan to submit. Sibling packages that depend on
this one: none recorded.

Any new vendored file that names an author means a new `Authors@R` entry:
CRAN asked for every one by name.

## Commands

Run from the package root. The everyday `devtools` loop is in
`.claude/shared/r-package.md`; these are the package's own.

```sh
R CMD INSTALL --preclean .              # when object files go stale
R CMD build . && R CMD check --as-cran zucsv_*.tar.gz
ZUCSV_STRESS=true Rscript -e 'devtools::test()'   # limits, millions of rows
Rscript bench/bench.R [reps]            # two-pass overhead vs read.csv()
tools/update-zsv.sh <commit-ish>        # re-vendor zsv, re-apply patches
Rscript tools/update-csv-spectrum.R     # regenerate test-spectrum.R
Rscript tools/check-csv-test-data.R     # csv-test-data conformance check
```

After editing C sources, `devtools::load_all()` recompiles; use
`R CMD INSTALL --preclean .` when object files go stale.

## Architecture

The design splits responsibility at one line (design §28):

```text
zsv owns CSV syntax        →  where rows and fields begin and end
zucsv owns R semantics     →  NA matching, column types, data.frame
                              construction, error messages
```

Consequences that shape most implementation decisions:

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
  allocation must be released on R errors as well as on success
  (`R_UnwindProtect()` or an equivalent discipline), and the row loop calls
  `R_CheckUserInterrupt()` periodically (about every 16k rows), not per row.
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
- **`test-upstream.R` pins the vendored parser's edge behaviour** recorded
  in `tools/zsv-behavior.md`; it is what catches a change on re-vendoring.
- **`test-differential.R` compares with `utils::read.csv()`** where the
  semantics overlap, with name repair off (zucsv does not repair names,
  design §5).
- **Stress tests are gated on `ZUCSV_STRESS=true`** rather than
  `skip_on_cran()` (`skip_unless_stress()`): they check limits, not memory
  safety, and under gctorture they would cost hours.

## Continuous integration

- `R-CMD-check.yaml` calls `r-cmd-check.yml@v1`: the GitHub runners plus the
  R-hub containers matching CRAN's r-devel clang and GCC flavours, which see
  the bundled parser's warnings before CRAN's pretest does.
- `native-checks.yaml` calls r-actions' `vendor.yml` (a change under
  `src/vendor` must also change `src/vendor/zsv/UPSTREAM`, the manifest),
  `sanitizers.yml` (with `asan: true`), `valgrind.yml` (with
  `--leak-check=full`, so leaks on error and interrupt paths carry a stack),
  `lto.yml`, `gctorture.yml`, `rchk.yml` (`fail-on-findings: true`) and
  `cran-special.yml` (full only). It runs weekly on Tuesdays and on dispatch.
- `coverage.yaml` calls `coverage.yml@v1`; `pkgdown.yaml` builds the site on
  pushes to `main`.

**Deviations from `.claude/shared/ci.md`, as of 2026-11-11:** no workflow
calls `changes.yml`. `native-checks.yaml` skips prose-only changes with a
`paths-ignore` list instead, and `R-CMD-check.yaml` deliberately has none,
because `README.md` ships in the tarball and `--as-cran` checks its URLs.
`coverage.yaml` is pinned to `@v1`, not to a commit. Bring these in line
with `ci.md` in their own pull request.

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

After an upgrade, re-verify `tools/zsv-behavior.md` (what the parser does at
the edges the design depends on: blank records, BOM, oversize rows,
unbalanced quotes); `tools/update-zsv.sh` does not rewrite it.
