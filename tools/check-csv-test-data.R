#!/usr/bin/env Rscript
#
# Checks zucsv against csv-test-data (https://github.com/sineemore/csv-test-data),
# a small RFC 4180 conformance suite.
#
#   Rscript tools/check-csv-test-data.R
#
# Unlike csv-spectrum, these fixtures are NOT transcribed into the test suite:
# the upstream repository declares no licence, so the files may not be
# redistributed. This script clones them at run time instead, and nothing from
# it ships in the package.
#
# It is a regression check, not a report. Every fixture has a recorded
# expectation below, including the seven where zucsv deliberately differs from
# the suite, each with the design section that decides it. The script exits
# non-zero if reality stops matching that table -- so an accidental change to
# blank-record handling or quote leniency shows up here.

suppressMessages({library(zucsv); library(jsonlite)})
repo <- "https://github.com/sineemore/csv-test-data.git"
work <- tempfile("csv-test-data-"); dir.create(work)
on.exit(unlink(work, recursive = TRUE), add = TRUE)
stopifnot(system2("git", c("clone", "--quiet", repo, shQuote(work))) == 0L)
sha <- trimws(system2("git", c("-C", shQuote(work), "rev-parse", "HEAD"), stdout = TRUE))

# fixture -> expected outcome. "match" means we agree with the suite;
# anything else is a deliberate deviation and says why.
expected <- c(
  `empty-field`                     = "match",
  `header-simple`                   = "match",
  `leading-space`                   = "match",
  `one-column`                      = "match",
  `quotes-empty`                    = "match",
  `quotes-with-comma`               = "match",
  `quotes-with-escaped-quote`       = "match",
  `quotes-with-newline`             = "match",
  `quotes-with-space`               = "match",
  `simple-crlf`                     = "match",
  `simple-lf`                       = "match",
  `trailing-newline`                = "match",
  `trailing-newline-one-field`      = "match",
  `trailing-space`                  = "match",
  `utf8`                            = "match",
  `bad-header-less-fields`          = "reject",
  `bad-header-more-fields`          = "reject",
  `bad-missing-quote`               = "reject",

  # Deliberate deviations.
  `all-empty`                       = "differ: blank records are skipped everywhere (SS10, decision 2); the suite wants two rows of one empty field",
  `empty-one-column`                = "differ: same as all-empty -- in a one-column file a bare blank line is skipped, and an intentional empty cell is written \"\" (SS10)",
  `header-no-rows`                  = "differ: representational -- a header-only file gives 0 rows but keeps its columns (SS14); the suite's [] cannot express column names",
  `bad-header-no-header`            = "accept: an empty file is a 0x0 data frame (SS14); zucsv has no notion of a required header",
  `bad-header-wrong-header`         = "accept: the suite checks the header equals foo,bar,baz, which is schema validation, not CSV parsing",
  `bad-quotes-with-unescaped-quote` = "accept: a quote inside an unquoted cell is passed through, as Excel does (SS14, tools/zsv-behavior.md); only zsv's unused SIMD engine rejects it",
  `bad-unescaped-quote`             = "accept: same as bad-quotes-with-unescaped-quote"
)

classify <- function(nm) {
  csvf <- file.path(work, "csv", paste0(nm, ".csv"))
  jsonf <- file.path(work, "json", paste0(nm, ".json"))
  has_header <- grepl("^(bad-)?header", nm)
  got <- tryCatch(read_csv(csvf, header = has_header, col_types = "character", na = NULL),
                  error = function(e) e)

  if (!file.exists(jsonf))
    return(if (inherits(got, "error")) "reject" else "accept")
  if (inherits(got, "error")) return("error")

  w <- fromJSON(jsonf, simplifyVector = FALSE)
  if (length(w) && !is.null(names(w[[1]]))) {
    want <- do.call(rbind, lapply(w, function(r) unlist(lapply(r, as.character))))
    if (!identical(names(got), names(w[[1]]))) return("differ")
  } else {
    rows <- lapply(w, function(r) unlist(lapply(r, as.character)))
    want <- if (length(rows)) do.call(rbind, rows) else matrix(character(0), 0, 0)
  }
  gm <- as.matrix(got); dimnames(gm) <- NULL; dimnames(want) <- NULL
  if (identical(dim(gm), dim(want)) && identical(as.vector(gm), as.vector(want)))
    "match" else "differ"
}

files <- sort(sub("\\.csv$", "", basename(Sys.glob(file.path(work, "csv", "*.csv")))))
cat("csv-test-data at ", substr(sha, 1, 8), ", ", length(files), " fixtures\n\n", sep = "")

bad <- character()
for (nm in files) {
  got <- classify(nm)
  exp <- expected[[nm]]
  if (is.null(exp)) { bad <- c(bad, sprintf("%s: new fixture upstream (got %s)", nm, got)); next }
  want <- sub(":.*", "", exp)
  ok <- identical(got, want)
  if (!ok) bad <- c(bad, sprintf("%s: expected %s, got %s", nm, want, got))
  cat(sprintf("  %-34s %-8s %s\n", nm, got,
              if (!ok) "<<< UNEXPECTED" else if (want == "differ") sub("^differ: ", "", exp) else ""))
}
missing <- setdiff(names(expected), files)
if (length(missing)) bad <- c(bad, sprintf("fixture gone upstream: %s", paste(missing, collapse = ", ")))

n_match <- sum(vapply(files, function(f) classify(f) %in% c("match", "reject"), NA))
cat(sprintf("\n%d of %d agree with the suite; %d deliberate deviations.\n",
            n_match, length(files), length(files) - n_match))
if (length(bad)) {
  cat("\nUNEXPECTED:\n"); cat(paste0("  ", bad, collapse = "\n"), "\n")
  quit(status = 1)
}
cat("All outcomes as recorded.\n")
