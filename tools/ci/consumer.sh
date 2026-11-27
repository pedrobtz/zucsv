#!/usr/bin/env bash
# The consumer fixture of #23, end to end: zucsv from this checkout, then
# tools/zucsvlink, which has only LinkingTo: zucsv, zuarrow and links the
# installed libzucsv.a and libzuarrow.a by its own ./configure, as zugodb
# will. Called by consumer.yaml on Linux, macOS and Windows. Run from the
# package root with testthat and zuarrow (DESCRIPTION's Remotes) installed.
# Adapted from zuarrow's tools/ci/consumer.sh.
set -eu

# Relative paths: on Windows the compiler is a native binary that does not
# understand a Git Bash /tmp path.
WORK=./.check-consumer
LIB="$WORK/lib"
rm -rf "$WORK"
mkdir -p "$LIB"
trap 'rm -rf "$WORK"' EXIT

echo "==> installing zucsv"
R CMD INSTALL --preclean --no-multiarch --library="$LIB" .

echo "==> the archive and its headers are installed"
ZUCSV_LIB=$(R_LIBS="$LIB" Rscript --vanilla -e 'arch <- .Platform$r_arch; d <- if (nzchar(arch)) system.file("lib", arch, package = "zucsv") else ""; if (!nzchar(d)) d <- system.file("lib", package = "zucsv"); cat(d)')
[ -f "$ZUCSV_LIB/libzucsv.a" ] || { echo "FAIL: libzucsv.a is not installed" >&2; exit 1; }
for f in include/zucsv.h include/zucsv-r.h; do
  [ -f "$LIB/zucsv/$f" ] || { echo "FAIL: $f is not installed" >&2; exit 1; }
done

echo "==> zucsv.h compiles with no R on the include path"
CC=$(R CMD config CC)
printf '#include "zucsv.h"\nint zucsv_header_check(void) { return ZUCSV_ABI_VERSION; }\n' > "$WORK/header.c"
$CC -std=c99 -Wall -Werror -I"$LIB/zucsv/include" -c "$WORK/header.c" -o "$WORK/header.o"

echo "==> the fixture declares no run-time dependency on zucsv or zuarrow"
if grep -qE '^(Imports|Depends):' tools/zucsvlink/DESCRIPTION; then
  echo "FAIL: zucsvlink declares Imports or Depends; it must link, not load" >&2
  exit 1
fi
if grep -qE '^\s*(import|importFrom)\(' tools/zucsvlink/NAMESPACE; then
  echo "FAIL: zucsvlink imports a namespace; it must link, not load" >&2
  exit 1
fi

echo "==> installing the fixture (its configure resolves both archives)"
R_LIBS="$LIB" R CMD INSTALL --preclean --no-multiarch --library="$LIB" tools/zucsvlink

# On macOS a dropped PKG_LIBS still links (R links with -undefined
# dynamic_lookup) and would fail only at load. On Linux and Windows the
# link itself fails. Where nm can read the object, the archives' code must
# be in it and none of it exported.
so=$(find "$LIB/zucsvlink/libs" -name 'zucsvlink.*' | head -1)
if command -v nm >/dev/null 2>&1 && [ -n "$so" ] && [ "$(nm "$so" 2>/dev/null | wc -l | tr -d ' ')" != "0" ]; then
  echo "==> the archives are linked into the fixture and not exported"
  u=$(nm -u "$so" 2>/dev/null | grep -cE 'zucsv|zsv_|zuarrow' || true)
  [ "$u" = "0" ] || { echo "FAIL: $u zucsv, zsv or zuarrow symbols are undefined in $so" >&2; exit 1; }
  d=$(nm "$so" 2>/dev/null | grep -cE ' [Tt] _*zucsv_open_stream$' || true)
  [ "$d" != "0" ] || { echo "FAIL: zucsv_open_stream is not in $so" >&2; exit 1; }
  case "$(uname -s)" in
    Darwin) exported=$(nm -gU "$so" | grep -v 'R_init_zucsvlink' | grep -cE 'zucsv|zsv_|zuarrow|collate_header' || true) ;;
    Linux) exported=$(nm -D --defined-only "$so" | grep -v 'R_init_zucsvlink' | grep -cE 'zucsv|zsv_|zuarrow|collate_header' || true) ;;
    *) exported=0 ;;
  esac
  [ "$exported" = "0" ] || { echo "FAIL: $so exports $exported archive symbols" >&2; exit 1; }
fi

# Via a file, not `Rscript -e`: on Windows only the first line of a
# multi-line -e argument reaches R.
cat > "$WORK/run-tests.R" <<'RUN_TESTS_R'
results <- as.data.frame(testthat::test_local(
  "tools/zucsvlink", reporter = "summary", load_package = "installed"))
# A zero-row result proved nothing.
if (nrow(results) == 0L || sum(results$passed) == 0L) {
  stop("no consumer tests ran")
}
quit(status = as.integer(any(results$failed > 0 | results$error)))
RUN_TESTS_R

echo "==> the fixture's tests, with zucsv as the oracle"
R_LIBS="$LIB" ZUCSVLINK_ORACLE=true Rscript --vanilla "$WORK/run-tests.R"

echo "==> the fixture's tests with zucsv and zuarrow absent from the library"
# A runner's library may hold a CRAN zucsv or zuarrow, so the run gets a
# library of its own: every package reachable now except those two, and
# nothing else beside R's own.
cat > "$WORK/without.R" <<'WITHOUT_R'
dest <- commandArgs(trailingOnly = TRUE)[[1]]
dir.create(dest, showWarnings = FALSE)
for (lib in setdiff(.libPaths(), .Library)) {
  for (p in setdiff(list.files(lib), c("zucsv", "zuarrow", list.files(dest)))) {
    if (!file.copy(file.path(lib, p), dest, recursive = TRUE)) {
      stop("could not copy ", p, " from ", lib)
    }
  }
}
WITHOUT_R
R_LIBS="$LIB" Rscript --vanilla "$WORK/without.R" "$WORK/without"
cat > "$WORK/absent.R" <<'ABSENT_R'
for (p in c("zucsv", "zuarrow")) {
  if (nzchar(system.file(package = p))) {
    stop(p, " is still reachable at ", system.file(package = p))
  }
}
ABSENT_R
# Nonexistent directories, so neither the user nor the site library is on
# the path.
NONE="$WORK/no-library"
R_LIBS="$WORK/without" R_LIBS_USER="$NONE" R_LIBS_SITE="$NONE" \
  Rscript --vanilla "$WORK/absent.R"
R_LIBS="$WORK/without" R_LIBS_USER="$NONE" R_LIBS_SITE="$NONE" \
  Rscript --vanilla "$WORK/run-tests.R"
echo "==> the consumer builds, links and reads CSV through the stream on $(uname -s)"
