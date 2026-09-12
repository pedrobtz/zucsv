# Benchmark results

Reproduce with `Rscript bench/bench.R [reps]`. Timings below: macOS 15
(x86_64-apple-darwin20), R 4.5.2, Apple clang 17, 3 reps, minimum elapsed
time. `data.table` 1.17.8, `vroom` 1.7.1. `readr` was not installed on this
machine; `bench.R` includes it automatically when it is.

**All fread numbers here are single-threaded.** `getDTthreads()` reports 1
and data.table reports *"has not been compiled with OpenMP support"* on this
machine (Apple clang ships no OpenMP). On a build with OpenMP, fread would be
faster still on numeric and integer input; on character input its threads
make it slower (0.415 s vs 0.374 s when forced on), because R string creation
is serial for everyone.

The bar for v0.1 is not "fastest reader". It is that the deliberate two-pass
design (design §9) introduces no avoidable overhead, measured end to end —
including R allocation, string creation and type conversion, not just raw
parser throughput.

## How `vroom` is measured

`vroom` returns ALTREP columns that defer parsing until a value is touched,
so timing the call alone measures setting up a mapping, not reading data. It
is reported three ways:

* **lazy** — what the call costs. Fast, but the work has not happened.
* **materialised** — `vroom:::vroom_materialize(replace = TRUE)`, which
  produces ordinary vectors.
* **altrep = FALSE** — `vroom`'s own switch for the same thing.

The last two normally agree to within a few percent, which is the check that
the forcing is real (in this run the character row's `altrep = FALSE` came
out at 1.03 s against 0.54 s materialised — a one-off; earlier runs had them
at 0.51 and 0.51). Merely *reading* each element — `nchar()` on a character
column, say — is **not** a valid force: `.Internal(inspect())` still shows
`materialized=F` afterwards, so it pays the parsing cost and produces nothing.

Compare `zucsv` against **materialised**, not lazy.

## Results

Seconds, lower is better. Best in each row in bold.

| Shape | Size | `zucsv` | `read.csv` | `fread` | `vroom` lazy | `vroom` mat. |
|---|---|---|---|---|---|---|
| numeric-heavy, narrow (200k × 10) | 17.9 MB | 0.149 | 1.785 | **0.069** | *0.037* | 0.088 |
| integer-heavy, narrow (200k × 10) | 14.1 MB | 0.158 | 1.327 | **0.034** | *0.036* | 0.077 |
| character-heavy, narrow (200k × 10) | 28.6 MB | 0.493 | 1.191 | **0.391** | *0.037* | 0.544 |
| mixed, narrow (200k × 12) | 20.2 MB | 0.238 | 1.274 | **0.131** | *0.053* | 0.189 |
| mixed, wide (2k × 400) | 6.7 MB | 0.070 | 0.305 | **0.051** | *0.083* | 0.154 |
| mixed, tiny (200 × 12) | 24 KB | 0.00063 | 0.00111 | **0.00057** | *0.00360* | 0.00424 |

Relative to `zucsv` (>1 means that reader is faster):

| Shape | `read.csv` | `fread` | `vroom` materialised |
|---|---|---|---|
| numeric-heavy | 0.08× (zucsv 12.0× faster) | 2.2× | 1.7× |
| integer-heavy | 0.12× (zucsv 8.4× faster) | 4.6× | 2.1× |
| character-heavy | 0.41× (zucsv 2.4× faster) | 1.26× | **0.91× (zucsv faster)** |
| mixed, narrow | 0.19× (zucsv 5.4× faster) | 1.8× | 1.3× |
| mixed, wide | 0.23× (zucsv 4.4× faster) | 1.4× | **0.45× (zucsv 2.2× faster)** |
| mixed, tiny | 0.57× (zucsv 1.8× faster) | 1.1× | **0.15× (zucsv 6.7× faster)** |

## What changed since the first run

The first version of this file had zucsv at 0.228 s on the numeric file.
Three changes, each sized by measurement before it was written, took it to
0.149 s. The method was an ablation of the per-cell work — parse each cell,
then switch each operation off in turn — which gave this profile for the
numeric file, in ns per cell:

| operation | ns/cell | outcome |
|---|---|---|
| `memcpy` + `R_strtod` | 46.6 | replaced by a transcription of `R_strtod5` on `(ptr, len)` with exactness shortcuts: 18.2 ns, bit-identical; the rest is the one `long double` divide R's algorithm requires, which fread pays too |
| bare zsv traversal, ×2 passes | 27.4 | untouched; the second pass is a design choice (§9) |
| grammar scans (mostly `is_double`) | 12.8 | untouched |
| NUL scan | 6.8 | skipped for any cell a grammar accepted (ASCII by construction) |
| UTF-8 validation | 4.5 | same |
| `VECTOR_ELT()` + `REAL()` per cell | 4.0 | pointers hoisted once per column |
| NA matching | 1.2 | left alone |

Two things the profile corrected. Fusing the three grammar scans, which
looked worth ~25 ns, measured at 2 ns for the two cheap ones and is not
worth doing. And "R_strtod" was 46% of the total but its replacement did not
need the correctly-rounded-vs-base-R decision it seemed to force: R's own
algorithm, transcribed, is 2.35× faster and gives the same bits.

## Reading

**Against `read.csv`: 1.8×–12× faster on every shape, never slower.** That
settles the question the benchmark exists to answer — reading the file twice
costs a fraction of what `read.csv` spends on one pass.

**Against `fread`: slower everywhere, by 1.1× to 4.6×, single-threaded on
both sides.** The gap is now widest on **integer** data, not numeric: integer
cells never went through `R_strtod`, so the conversion work did not help them,
and what is left is two traversals plus `is_integer` run twice (once to
infer, once to convert). Narrowest on character (1.26×), where the cost is
creating R strings and no reader escapes it: zucsv's R layer there is
0.37 s, and so is fread's entire runtime.

**Against `vroom`, materialised: mixed, and `zucsv` wins where string data
lives.** `vroom` is 1.3–2.1× faster on numeric, integer and mixed-narrow
input. `zucsv` is faster on character-heavy input, on wide tables (2.2×) and
on small files (6.7×, where `vroom`'s ~3.5 ms setup dominates). The lazy
column is why `vroom` is often assumed to be far ahead: it is 13× faster than
`zucsv` on the character file until the data is touched, at which point it
is behind.

**Throughput is 58–120 MB/s across shapes**, including the 400-column table.
Nothing pathological appears when the table is wide, and the small-file case
is not dominated by setup — the opposite of `vroom` there.

## Where the remaining time is

Per cell on the numeric file zucsv is now at ~72 ns against fread's 31. Of
that, 27 ns is parsing the file twice and ~13 ns is the grammar scan that
pass 2 repeats. Those two are the same design choice — §9's second pass —
and are worth roughly 40% of what is left. Beyond them, the honest ceiling on
one core is around fread's figure, not below it.

On character data the picture is different and better. R's string cache
costs ~75 ns per *unique* string but only ~22 ns for a repeat, and a
run-length reuse of the previous `CHARSXP` — one `memcmp` instead of a
hash — measured 2–4× faster than `mkCharLenCE()` on real categorical columns
(KEN_ALL's prefecture column: 24.1 → 8.0 ns/cell), at a 7% cost when nothing
repeats. fread has no such reuse. That is where beating it on real data lives.
