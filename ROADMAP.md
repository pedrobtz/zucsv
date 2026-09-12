# zucsv — Roadmap to v0.1.0 on CRAN

Companion to `zucsv-design.md`; section references (§) point there.

Six stages, each ending in a green `R CMD check` on the three CI platforms. A stage is not done until its exit criteria hold — later stages assume them. Stages 2–3 are where the package takes shape; 0–1 remove CRAN surprises early, 4–5 are hardening and submission.

Rough sizing: 0 and 5 are small, 1 depends on what the vendored code audit turns up, 2–4 are the bulk of the work.

**Progress:** Stages 0–3 are complete (one commit each). Stage 4 is next.
Findings from the finished stages have been folded back into
`zucsv-design.md`; `tools/zsv-behavior.md` is the record of what the vendored
parser actually does.

---

## Stage 0 — Package skeleton that already passes check

**Goal:** an installable, checkable package with correct metadata, so every later stage is measured against a clean baseline.

**Work**

- `DESCRIPTION`: real `Title`, `Description` (naming `'zsv'` with its URL), `Authors@R` (package author `aut, cre`; zsv copyright holder `cph`), `Depends: R (>= 4.2.0)`, `URL`, `BugReports`. Version stays `0.0.0.9000` until Stage 5.
- `LICENSE`: fill `YEAR` / `COPYRIGHT HOLDER`.
- `.Rbuildignore`: add `^CLAUDE\.md$`, `^zucsv-design\.md$`, `^ROADMAP\.md$`, `^bench$`.
- `usethis::use_news_md()`, `usethis::use_cran_comments()`.
- `devtools::document()` so `NAMESPACE` gains `useDynLib(zucsv, .registration = TRUE)`.
- `src/init.c` with an empty registration table (`R_registerRoutines`, `R_useDynamicSymbols(FALSE)`, `R_forceSymbols(TRUE)`); delete the placeholder `src/zucsv-package.c`.
- `tests/testthat/test-package.R` with a trivial test so the test harness is exercised.

**Exit criteria**

- `R CMD check --as-cran` clean locally and on all CI matrix entries.
- `README.md` still has placeholder prose — acceptable until Stage 5.

---

## Stage 1 — Vendor `zsv` and prove it compiles everywhere

**Goal:** the parser subset builds inside the package under CRAN's compiler flags on Linux, macOS, and Windows, with the audit in §17 complete and recorded.

**Work**

- Pick the upstream commit. Record repo/tag/commit/date in `src/vendor/zsv/UPSTREAM`; copy the upstream `LICENSE`.
- Identify the minimal parser subset (headers + the sources the pull API needs; nothing CLI, SQL, JSON, or utf8proc-dependent). Write `tools/update-zsv.sh` that re-copies that subset for a given commit and applies `tools/patches/*.patch`.
- `src/Makevars` / `src/Makevars.win`: explicit `OBJECTS`, `PKG_CPPFLAGS` include paths, no optimisation/arch/diagnostic flags, no GNU make (§19).
- Audit the subset (§17): grep for `exit`, `abort`, `assert`, `printf`, `stderr`, `rand`; compile with `-Wall -Wextra -pedantic -Wstrict-prototypes` on GCC and Clang and with a C23 standard. Fix with minimal patches, each documented in `UPSTREAM` with a reason; report genuine fixes upstream.
- Confirm and write down the behaviors §10/§13/§15 depend on: pull API present, what an empty line yields, whether a BOM is stripped, what happens to an oversize row (status code and whether parsing can continue), unbalanced-quote recovery. Turn each into a fixture in Stage 2.
- `inst/COPYRIGHTS` and `LICENSE.note`.
- Temporary smoke entry point `C_zsv_smoke(file)` returning `c(records, cells)` — internal, not exported, removed in Stage 2 — to prove the parser runs from R on all platforms.

**Exit criteria**

- Compiles warning-free under the flags above on all CI entries; `R CMD check --as-cran` clean.
- `UPSTREAM` lists commit and every patch; `inst/COPYRIGHTS` present.
- Behavior questions above answered in writing (a short "Verified upstream behavior" section appended to `UPSTREAM` or to the design doc).

---

## Stage 2 — End-to-end reader, character columns only

**Goal:** `read_csv()` works for real files with every structural feature of the design, returning all-character data frames. Types come next; getting structure, errors, and resource handling right first keeps Stage 3 purely about `convert.c`.

**Work**

- `src/zucsv.h`: reader struct, constants (column cap, record-size cap, interrupt interval, message truncation length), pass state.
- `src/read_csv.c`:
  - `C_read_csv()` entry, argument re-validation in C;
  - reader struct + `R_UnwindProtect()` body/cleanup (§16); all scratch via `R_alloc()`;
  - open file, configure parser (delimiter, column cap, record-size cap), BOM handling;
  - pass 1: header, width, record numbering, blank-record skipping, NUL rejection, `na` matching, UTF-8 validation, row count, limits (§9, §10, §13, §15);
  - pass 2: allocate `STRSXP` columns, fill with `mkCharLenCE(..., CE_UTF8)` / `NA_STRING`, drift check against pass 1;
  - data-frame assembly: names, compact row names, class (§5);
  - error formatting with truncation (§20);
  - `R_CheckUserInterrupt()` every N records in both passes.
- `src/convert.c`: `na` table construction and matching, UTF-8 validator (the type grammars come in Stage 3).
- `R/read_csv.R`: exported wrapper, argument checks with R-level messages, `path.expand()`, roxygen skeleton. `col_types` accepted but only `"character"` honored; `NULL` temporarily means character.
- Tests (`tests/testthat/test-structure.R`, `test-errors.R`, `test-encoding.R`): every structural item in §21 — quoting, newlines, CRLF/LF, trailing delimiter, blank records in all positions, whitespace-only records, header/no-header, duplicate/empty names, BOM, UTF-8 cells/names/paths and `~`, invalid UTF-8, NUL, width mismatch messages, empty and header-only files, invalid delimiters, column cap and one beyond, long cells, `na` variants and `na = NULL`. Fixtures via `writeBin(tempfile())`.
- Upstream-behavior fixtures from Stage 1 (`test-upstream.R`).
- Differential test against `utils::read.csv(colClasses = "character", ...)`.
- Manual check: interrupt a multi-GB read; confirm no leaked handles (e.g. `lsof` on macOS/Linux) and that the R session is healthy afterwards.

**Exit criteria**

- All tests pass on CI; `R CMD check --as-cran` clean.
- Every error message in §20 that concerns structure is produced verbatim by a test.
- Interrupt/cleanup manually verified once and the procedure noted in `tests/README.md` or a comment.

---

## Stage 3 — Type inference and forced conversion

**Goal:** `convert.c` complete; `col_types = NULL` infers, explicit `col_types` forces, all per §11–§12.

**Work**

- Grammar checks on `{ptr, len}`: logical (four exact strings), integer (`[+-]?[0-9]+`), double (decimal/scientific, `.5`, `5.`, `Inf`/`-Inf`/`+Inf`/`NaN`).
- Integer accumulation in 64-bit with range check `[-2147483647, 2147483647]`; out-of-range promotes (inference) or errors (forced).
- Double conversion via scratch buffer + `R_strtod()`.
- Per-column inference state (`can_logical`, `can_integer`, `can_double`, `all_missing`) and final selection order; all-missing → character.
- Pass 1 validates forced types fully; pass 2 converts into `LGLSXP`/`INTSXP`/`REALSXP` and must never fail on content.
- Conversion error message with row, column, name, truncated value (§12, §20); `col_types` length mismatch message.
- Tests (`test-types.R`, `test-col-types.R`): every type item in §21 — rejected logical spellings, leading zeros, `+` sign, `-2147483648`, overflow promotion, `Inf`/`NaN` and their rejected spellings, `" 42"`, all-missing columns, forced types length 1 and `ncol`, unknown names, each failure message.
- Randomized round-trip test: generate data frames with all four types plus `NA`s, `write.csv()`, `read_csv()`, `identical()`; fixed seed.
- Differential test against `utils::read.csv()` values on canonical numeric fixtures.

**Exit criteria**

- All tests pass; check clean.
- Inference rules in §11 are each pinned by at least one fixture, including the rejected spellings.
- `expect_identical()` round-trip holds for the generated data frames.

---

## Stage 4 — Hardening: memory safety, static analysis, benchmarks

**Goal:** the native code is demonstrably clean under the tools CRAN will run after acceptance, and the two-pass design is shown not to be pathologically slow.

**Work**

- CI job running the test suite under ASan/UBSan (`rocker/r-devel-san` container). Fix everything it reports, in vendored code too (as patches).
- R-hub v2 (`rhub::rhub_check()`) runs on the sanitizer, `valgrind`, `rchk`, `c23`, `noremap`, and `nold` platforms; fix everything.
- Stress tests behind `skip_on_cran()`: millions of rows, 65,536 columns, multi-MB single cell, file at the record-size cap, repeated failing parses in a loop (leak check via sanitizer job).
- Confirm the "file changed while reading" drift check by truncating a file between passes in a test that uses a small file and a `Sys.sleep`-free trick (e.g. a fixture whose second open is redirected); if that proves impractical, cover it by code review and leave a comment.
- `bench/` (in `.Rbuildignore`): scripts comparing `utils::read.csv()`, `readr::read_csv()`, `data.table::fread()`, `zucsv::read_csv()` on numeric-heavy, character-heavy, narrow, and wide files (§22). Record results in `bench/RESULTS.md`. The bar is "no avoidable overhead", not "fastest".
- Review `PROTECT` discipline by hand once `rchk` is clean; reading `read_csv.c` top to bottom with rchk's rules in mind catches the patterns rchk can miss.

**Exit criteria**

- Sanitizer CI job green and part of the required checks.
- R-hub sanitizer/valgrind/rchk runs clean.
- `bench/RESULTS.md` exists and shows `zucsv` within a small factor of `read.csv()` on every shape (faster is expected; slower on any shape needs an explanation in the file).

---

## Stage 5 — Documentation, release checks, CRAN submission

**Goal:** the package on CRAN.

**Work**

- Roxygen: full `@param` docs, `@return` (mandatory for CRAN), `@examples` that write a small CSV to `tempfile()` and read it back; `@details` summarising §10–§13 semantics (blank rows, numbering, inference rules, strict errors) since they are the user-facing contract.
- `README.md`: real example, one paragraph on what `zsv` is, link to the design doc's semantics; keep `pak::pak("pedrobtz/zucsv")` install line and add the CRAN line commented until acceptance.
- `NEWS.md`: `# zucsv 0.1.0` entry listing the API and the documented limitations.
- `cran-comments.md`: platforms checked, "new submission", note that `zsv` is bundled with `cph` and `inst/COPYRIGHTS`.
- `_pkgdown.yml` `url:` set; pkgdown build green.
- Optional but cheap: `spelling::spell_check_package()`, `urlchecker::url_check()`.
- Bump `Version: 0.1.0`.
- Final checks: `devtools::check(remote = TRUE, manual = TRUE)`, `devtools::check_win_devel()`, `devtools::check_mac_release()`, R-hub `windows`, `macos-arm64`, `linux` on R-devel. Zero WARNINGs; zero NOTEs other than "New submission".
- Submit with `devtools::submit_cran()` (or the web form); confirm the email.
- Respond to reviewer feedback the same day where possible; typical asks are `Description` wording, `\value`, example runtime, bundled-code copyright — all pre-empted in §25 but expect at least one round.
- After acceptance: `git tag v0.1.0`, GitHub release, `usethis::use_dev_version()`, add the CRAN badge, watch the CRAN check results page for the first two weeks (ATLAS, noLD, M1mac, and the sanitizer runs report there and may email).

**Exit criteria**

- Package visible on CRAN; check results page green (or all NOTEs explained).
- Tag pushed, `NEWS.md` matches the released version.

---

## Open questions to settle before Stage 2 starts

These are decisions in `zucsv-design.md` §29 that a stakeholder may still want to reverse. Changing them later means rewriting fixtures, so confirm now:

1. Blank records skipped everywhere (§29 #2) — the single-column `""` caveat is the cost.
2. `Inf`/`NaN` as double syntax (§29 #3).
3. Leading-zero integers (§29 #4) — ZIP codes become integers unless forced.
4. Invalid UTF-8 is an error rather than passed through (§29 #6) — Latin-1 files will not read until `encoding=` exists in v0.2.
5. `R (>= 4.2.0)` floor (§29 #1).

## Risks

| Risk | Where it bites | Mitigation |
|------|----------------|------------|
| Vendored `zsv` uses `assert`/`fprintf(stderr)`/non-pedantic C | Stage 1, and again at every upgrade | Audit + patch files + `update-zsv.sh`; report upstream so patches shrink over time. |
| Pull API absent or different in pinned commit | Stage 1 | §8 names the push-API fallback; the row function is the same either way. |
| Empty-line / BOM / oversize-row semantics differ from assumptions | Stage 1–2 | §10/§13/§15 define `zucsv` policy independently of the parser; Stage 1 writes down what the parser does, Stage 2 makes fixtures enforce the policy. |
| `rchk` or ASan findings late | Stage 4 | Run the sanitizer CI job from Stage 2 onward, not only in Stage 4. |
| Two-pass cost on huge character-heavy files | Stage 4 benchmarks | Accepted for v0.1 (§9); the one-pass builder is a v0.2 candidate if numbers demand it. |
| CRAN reviewer asks for changes to bundled-code copyright wording | Stage 5 | `cph` role + `inst/COPYRIGHTS` + `LICENSE.note` prepared in Stage 1; reference them in `cran-comments.md`. |
