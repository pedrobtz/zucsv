#!/bin/sh
# Re-vendor the zsv parser subset used by zucsv.
#
# Usage: tools/update-zsv.sh <commit-ish> [repo-url]
#
# Copies only the files listed in FILES below into src/vendor/zsv/, generates
# zsv.h from zsv.h.in, re-applies every patch in tools/patches/ in sorted
# order, and rewrites src/vendor/zsv/UPSTREAM.
#
# After running this, re-read the "Verified upstream behavior" section of
# UPSTREAM: it records what the parser does at the edges zucsv depends on
# (blank records, BOM, oversize rows, unbalanced quotes), and an upgrade can
# change any of it. tests/testthat/test-upstream.R is the enforcement.

set -eu

COMMIT=${1:?usage: tools/update-zsv.sh <commit-ish> [repo-url]}
REPO=${2:-https://github.com/liquidaty/zsv.git}

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST=$ROOT/src/vendor/zsv
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# The parser core only. src/zsv.c is a unity build that #includes the other
# .c files, so they are copied but never compiled on their own; see
# src/Makevars, which lists vendor/zsv/src/zsv.o as the only vendored object.
FILES="
include/zsv/common.h
include/zsv/api.h
include/zsv/zsv_export.h
include/zsv/utils/compiler.h
include/zsv/utils/utf8.h
include/zsv/utils/string.h
src/zsv.c
src/zsv_internal.c
src/zsv_scan_delim.c
src/zsv_scan_delim_fast.c
src/zsv_scan_fixed.c
src/zsv_strencode.c
src/vector_delim.c
src/zsv_scan_simd_avx2.h
src/zsv_scan_simd_neon.h
src/zsv_scan_simd_sse2.h
src/zsv_scan_simd_wasm.h
LICENSE
"

echo "Cloning $REPO"
git clone --quiet "$REPO" "$WORK/zsv"
git -C "$WORK/zsv" checkout --quiet "$COMMIT"
RESOLVED=$(git -C "$WORK/zsv" rev-parse HEAD)
DESCRIBE=$(git -C "$WORK/zsv" describe --tags --always 2>/dev/null || echo "$RESOLVED")

rm -rf "$DEST"
for f in $FILES; do
  mkdir -p "$DEST/$(dirname "$f")"
  cp "$WORK/zsv/$f" "$DEST/$f"
done

# Upstream generates zsv.h from zsv.h.in by substituting the ZSV_EXTRAS
# define. zucsv never builds with extras, so the placeholder becomes a
# comment recording that.
sed 's|__ZSV_EXTRAS__DEFINE__|/* ZSV_EXTRAS intentionally not defined by zucsv */|' \
  "$WORK/zsv/include/zsv.h.in" > "$DEST/include/zsv.h"

for p in "$ROOT"/tools/patches/*.patch; do
  [ -e "$p" ] || continue
  echo "Applying $(basename "$p")"
  patch -d "$DEST" -p1 --quiet < "$p"
done

cat > "$DEST/UPSTREAM" <<EOF
Upstream:  $REPO
Describe:  $DESCRIBE
Commit:    $RESOLVED
Vendored:  $(date -u +%Y-%m-%d)
By:        tools/update-zsv.sh
License:   MIT (see LICENSE in this directory)

Only the parser core is vendored. The zsv CLI application, and the SQL,
SQLite, JSON, sheet-viewer and extension facilities, are not part of zucsv.
ZSV_EXTRAS is never defined, which is what keeps <sqlite3.h> and
<zsv/utils/arg.h> out of the build.

Verified behavior for this commit: ../../../tools/zsv-behavior.md
Re-verify that file on every upgrade; tests/testthat/test-upstream.R is
what catches a change.

Patches applied (tools/patches, in sorted order):
EOF

if ls "$ROOT"/tools/patches/*.patch >/dev/null 2>&1; then
  for p in "$ROOT"/tools/patches/*.patch; do
    printf '  %-40s %s\n' "$(basename "$p")" \
      "$(sed -n 's/^# Reason: //p' "$p" | head -1)" >> "$DEST/UPSTREAM"
  done
else
  echo "  (none)" >> "$DEST/UPSTREAM"
fi

echo "Vendored $DESCRIBE ($RESOLVED) into $DEST"
