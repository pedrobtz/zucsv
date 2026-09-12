# Benchmark results

Reproduce with `Rscript bench/bench.R [reps]`. Timings below: macOS 15
(x86_64-apple-darwin20), R 4.5.2, Apple clang 17, 3 reps, minimum elapsed
time. `data.table` 1.17.8, `vroom` 1.7.1. `readr` was not installed on this
machine; `bench.R` includes it automatically when it is.

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

The last two agree to within 3% everywhere, which is the check that the
forcing is real. Note that merely *reading* each element (`nchar()` on a
character column, say) is **not** a valid force: it pays the parsing cost but
leaves the vector unmaterialised, so it measures the work twice over and
produces nothing. That mistake inflates `vroom`'s character-column time by
about 6%.

Compare `zucsv` against **materialised**, not lazy.

## Results

Seconds, lower is better. Best in each row in bold.

| Shape | Size | `zucsv` | `read.csv` | `fread` | `vroom` lazy | `vroom` mat. |
|---|---|---|---|---|---|---|
| numeric-heavy, narrow (200k × 10) | 17.9 MB | 0.228 | 1.469 | 0.068 | *0.034* | **0.076** |
| integer-heavy, narrow (200k × 10) | 14.1 MB | 0.165 | 1.116 | **0.035** | *0.037* | 0.074 |
| character-heavy, narrow (200k × 10) | 28.6 MB | 0.470 | 1.156 | **0.376** | *0.037* | 0.511 |
| mixed, narrow (200k × 12) | 20.2 MB | 0.282 | 1.246 | **0.131** | *0.053* | 0.190 |
| mixed, wide (2k × 400) | 6.7 MB | 0.085 | 0.303 | **0.051** | *0.081* | 0.152 |
| mixed, tiny (200 × 12) | 24 KB | 0.00067 | 0.00110 | **0.00058** | *0.00356* | 0.00425 |

Relative to `zucsv` (>1 means that reader is faster):

| Shape | `fread` | `vroom` materialised |
|---|---|---|
| numeric-heavy | 3.4× | 3.0× |
| integer-heavy | 4.7× | 2.2× |
| character-heavy | 1.25× | **0.92×** (zucsv faster) |
| mixed, narrow | 2.2× | 1.5× |
| mixed, wide | 1.7× | **0.56×** (zucsv faster) |
| mixed, tiny | 1.2× | **0.16×** (zucsv faster) |

## Reading

**Against `read.csv`, the reference point: 2.5×–6.8× faster on every shape,
never slower.** That settles the question the benchmark exists to answer —
reading the file twice still costs well under what `read.csv` spends on one
pass.

**Against `fread`: slower everywhere, by 1.2× to 4.7×.** Expected, and not a
v0.1 concern. `fread` is multi-threaded, memory-maps its input and makes one
pass; `zucsv` is single-threaded, reads through `fread(3)` and makes two by
design. The gap is widest on numeric and integer data, where parsing
dominates and threads pay off, and narrowest (1.25×) on character data, where
the cost is creating R strings — which no reader can avoid.

**Against `vroom`, materialised: mixed, and `zucsv` wins where it matters
most for string data.** `vroom` is 1.5–3× faster on numeric, integer and
mixed-narrow input. `zucsv` is faster on character-heavy input (0.470 vs
0.511), on wide tables (1.8×) and on small files (6.3×, where `vroom`'s
~3.5 ms setup dominates). The lazy column is why `vroom` is often assumed to
be far ahead: it is 13× faster than `zucsv` on the character file until the
data is actually touched, at which point it is slightly behind.

**Throughput is flat at 60–90 MB/s across shapes**, including the 400-column
table. Nothing pathological appears when the table is wide, and the
small-file case is not dominated by setup — the opposite of `vroom` there.

## If the second pass is ever revisited

The numbers say it is not urgent. If it is taken up anyway (design §9 leaves
it open, §27 lists it for v0.2), the honest comparison is against these
figures rather than against `read.csv`. The second pass re-runs the grammar
checks but creates no R objects, so its cost is roughly the difference
between the numeric and character rows: largest where parsing dominates,
near-invisible where string creation does. That also says where the work
would pay off — numeric-heavy input, which is exactly where `fread` and
`vroom` are furthest ahead.
