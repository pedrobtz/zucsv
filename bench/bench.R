# Benchmarks for zucsv. Not part of the package build (bench/ is in
# .Rbuildignore), so the comparison packages never enter Suggests.
#
#   Rscript bench/bench.R [reps]
#
# The question is not "is zucsv the fastest reader" -- it is "does the two-pass
# design (design SS9) introduce avoidable overhead". The reference point is
# utils::read.csv(), which is always available; readr and data.table are
# included when installed.

reps <- as.integer(commandArgs(trailingOnly = TRUE)[1])
if (is.na(reps)) reps <- 5L

suppressMessages(library(zucsv))
has <- function(p) requireNamespace(p, quietly = TRUE)

make_file <- function(kind, nrow, ncol) {
  path <- tempfile(fileext = ".csv")
  set.seed(1)
  cols <- switch(
    kind,
    numeric = lapply(seq_len(ncol), function(i) round(rnorm(nrow), 6)),
    integer = lapply(seq_len(ncol), function(i) sample(-1e6:1e6, nrow, TRUE)),
    character = lapply(seq_len(ncol), function(i)
      replicate(nrow, paste0(sample(letters, 12, TRUE), collapse = ""))),
    mixed = lapply(seq_len(ncol), function(i) {
      switch(((i - 1L) %% 4L) + 1L,
             sample(-1e6:1e6, nrow, TRUE),
             round(rnorm(nrow), 6),
             replicate(nrow, paste0(sample(letters, 10, TRUE), collapse = "")),
             sample(c(TRUE, FALSE), nrow, TRUE))
    })
  )
  df <- as.data.frame(cols, stringsAsFactors = FALSE)
  names(df) <- paste0("c", seq_len(ncol))
  utils::write.csv(df, path, row.names = FALSE)
  path
}

# Batches the call until a run takes at least `floor` seconds, so that files
# small enough to read in under a clock tick still get a meaningful number
# instead of 0.000.
time_it <- function(expr, reps, floor = 0.2) {
  expr <- substitute(expr)
  env <- parent.frame()
  eval(expr, env) # warm

  batch <- 1L
  repeat {
    t <- system.time(for (i in seq_len(batch)) eval(expr, env))[["elapsed"]]
    if (t >= floor || batch >= 100000L) break
    batch <- batch * 10L
  }
  ts <- replicate(reps, {
    system.time(for (i in seq_len(batch)) eval(expr, env))[["elapsed"]]
  })
  min(ts) / batch
}

# Forces a lazily-read result, so readers that defer work with ALTREP are
# compared on the same footing as those that do not. Without this, vroom
# looks ~16x faster than it is for any use that touches the data.
#
# vroom:::vroom_materialize(replace = TRUE) is used rather than a loop that
# merely reads each element: reading through the ALTREP interface (nchar(),
# say) does the parsing work but leaves the vector unmaterialised, so it
# charges the cost without producing the vector, which is the wrong thing to
# measure. It is an internal function, hence the :::; this script is not part
# of the package.
materialise <- function(df) {
  vroom:::vroom_materialize(df, replace = TRUE)
}

readers <- list(
  `zucsv::read_csv` = function(p) zucsv::read_csv(p),
  `utils::read.csv` = function(p) utils::read.csv(p)
)
if (has("readr")) readers[["readr::read_csv"]] <-
  function(p) readr::read_csv(p, show_col_types = FALSE, progress = FALSE)
if (has("data.table")) readers[["data.table::fread"]] <-
  function(p) data.table::fread(p, showProgress = FALSE)
if (has("vroom")) {
  # Reported twice on purpose: "lazy" is what the call costs, "materialised"
  # is what reading the data costs.
  readers[["vroom (lazy)"]] <-
    function(p) vroom::vroom(p, show_col_types = FALSE, progress = FALSE)
  readers[["vroom (materialised)"]] <-
    function(p) materialise(vroom::vroom(p, show_col_types = FALSE, progress = FALSE))
  readers[["vroom (altrep=FALSE)"]] <-
    function(p) vroom::vroom(p, show_col_types = FALSE, progress = FALSE, altrep = FALSE)
}

shapes <- list(
  list(kind = "numeric",   nrow = 200000L, ncol = 10L, label = "numeric-heavy, narrow"),
  list(kind = "integer",   nrow = 200000L, ncol = 10L, label = "integer-heavy, narrow"),
  list(kind = "character", nrow = 200000L, ncol = 10L, label = "character-heavy, narrow"),
  list(kind = "mixed",     nrow = 200000L, ncol = 12L, label = "mixed, narrow"),
  list(kind = "mixed",     nrow = 2000L,   ncol = 400L, label = "mixed, wide"),
  list(kind = "mixed",     nrow = 200L,    ncol = 12L, label = "mixed, tiny")
)

cat("zucsv benchmarks\n")
cat("R ", R.version.string, "\n", sep = "")
cat("reps: ", reps, " (reporting the minimum elapsed time)\n\n", sep = "")

rows <- list()
for (s in shapes) {
  path <- make_file(s$kind, s$nrow, s$ncol)
  size_mb <- file.size(path) / 1024^2
  cat(sprintf("== %s: %d x %d, %.1f MB ==\n", s$label, s$nrow, s$ncol, size_mb))

  base_t <- NA_real_
  for (nm in names(readers)) {
    f <- readers[[nm]]
    t <- time_it(f(path), reps)
    if (nm == "utils::read.csv") base_t <- t
    rows[[length(rows) + 1L]] <- data.frame(
      shape = s$label, reader = nm, seconds = t,
      mb_per_s = size_mb / t, stringsAsFactors = FALSE
    )
  }
  for (i in seq_along(readers)) {
    r <- rows[[length(rows) - length(readers) + i]]
    cat(sprintf("  %-22s %9.5f s  %7.1f MB/s  %6.2fx read.csv\n",
                r$reader, r$seconds, r$mb_per_s, base_t / r$seconds))
  }
  cat("\n")
  unlink(path)
}

res <- do.call(rbind, rows)
saveRDS(res, file.path("bench", "results.rds"))
invisible(res)
