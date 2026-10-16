# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project state

`zucsv` 0.1.0 is on CRAN (published 2026-10-16); `main` is `0.1.0.9000`, working toward v0.1.1. `read_csv()` in
[R/read_csv.R](R/read_csv.R) is a thin wrapper over the C reader in `src/read_csv.c` and
`src/convert.c`, built on the vendored parser under `src/vendor/zsv/`; the suite lives in
`tests/testthat/`. The full design document is [.agents/zucsv-design.md](.agents/zucsv-design.md)
and the staged plan is [.agents/ROADMAP.md](.agents/ROADMAP.md). Both are build-ignored
(`^\.agents$`), so they stay out of the tarball.

**Read `.agents/zucsv-design.md` before writing any code.** It is the specification for v0.1 and
covers the public API, type-inference rules, error messages, two-pass strategy, resource limits,
the `src/` file layout, and the explicit non-goals. Treat its "Non-goals for v0.1" (§3) as binding:
do not add `write_csv()`, connections, URL input, date parsing, column selection, `skip`/`n_max`,
ALTREP, or a parser-mode switch unless the user asks to revise the design. §29 is the decision log —
check it before reopening a choice that looks arbitrary. `.agents/ROADMAP.md` says which stage the
work is in and what its exit criteria are; finish a stage's criteria before starting the next.

## Commands

Development uses `devtools`/`testthat` from an R session at the package root:

```r
devtools::load_all()                      # compile src/ and load, without installing
devtools::document()                      # regenerate NAMESPACE and man/ from roxygen
devtools::test()                          # run all tests
devtools::test(filter = "read_csv")       # run only tests/testthat/test-read_csv.R
testthat::test_file("tests/testthat/test-read_csv.R")
devtools::check()                         # R CMD check
```

From a shell:

```sh
R CMD INSTALL --preclean .
R CMD build . && R CMD check --as-cran zucsv_*.tar.gz
```

`devtools::document()` must be re-run after touching any roxygen block — `NAMESPACE` is generated
and must not be edited by hand. After editing C sources, `devtools::load_all()` recompiles; use
`R CMD INSTALL --preclean .` when object files go stale.

CI (`.github/workflows/R-CMD-check.yaml`) runs `R CMD check` on macOS, Windows, and Ubuntu
(devel/release/oldrel-1); `pkgdown.yaml` builds the site on pushes to `main`.

## Architecture

The design splits responsibility at one line (design §28):

```
zsv owns CSV syntax        →  where rows and fields begin and end
zucsv owns R semantics     →  NA matching, column types, data.frame construction, error messages
```

Consequences that shape most implementation decisions:

- **Base R C API only, via `.Call`.** No Rcpp, no cpp11, no runtime R package dependencies. Native
  symbols get registered in `src/init.c` with dynamic symbol lookup disabled;
  `R/zucsv-package.R` already carries the `@useDynLib zucsv, .registration = TRUE` tag.
- **Thin R wrapper.** `read_csv()` in `R/read_csv.R` normalizes arguments and calls one C routine.
  Any validation that affects native safety must also be enforced in C, not only in R.
- **Two passes over the file** (design §9): pass 1 counts rows, fixes the column count, applies `na`
  matching, and infers types; pass 2 allocates each column at its exact final length and writes
  converted cells straight into it. No growable vectors, no intermediate row lists. This is why
  v0.1 accepts only seekable local file paths.
- **`zsv` is vendored**, parser subset only, under `src/vendor/zsv/`, pinned to a recorded upstream
  commit. Never build against a system `zsv`. Keep upstream sources unmodified — R-specific
  adaptation belongs in `zucsv`'s own `src/` files. Record repo/tag/commit/date and the upstream
  license when vendoring.
- **Strict errors, never silent `NA`.** A failed forced conversion, a ragged row, or an embedded NUL
  is an error carrying row, column, and column-name context. Raw `zsv` status codes stay internal.
- **Long jumps are the hazard.** Every `FILE *`, `zsv_parser`, and native allocation must be released
  on R errors as well as on success (`R_UnwindProtect()` or an equivalent discipline), and the row
  loop calls `R_CheckUserInterrupt()` periodically (≈ every 16k rows), not per row.

Exported names are unprefixed (`zucsv::read_csv()`, not `zu_read_csv()`) — the package name is the
namespace.

## Releases

0.1.0 was published on CRAN on 2026-10-16, from commit `1cc69d9` (the resubmission that named every
`zsv` author in `Authors@R`). The tag is the bare version, `0.1.0`, not `v0.1.0` (still to be created, with its GitHub release, as of 2026-10-16). `main` moved to
`0.1.0.9000` right after, with a `# zucsv (development version)` heading in `NEWS.md`; record every
user-visible change under it.

The next release is v0.1.1, tracked in [#13](https://github.com/pedrobtz/zucsv/issues/13): attach
planned work to it as sub-issues. CRAN's cadence opens on 2026-11-27, six weeks after publication
([#14](https://github.com/pedrobtz/zucsv/issues/14)); that is the earliest a submission is allowed,
not a plan to submit. The design's "Non-goals for v0.1" (§3) still bind until a design revision lifts
one. Any new vendored file that names an author means a new `Authors@R` entry: CRAN asked for every
one by name.
