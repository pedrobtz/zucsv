# Changelog

## zucsv 0.1.0

First release.

- [`read_csv()`](https://pedrobtz.github.io/zucsv/reference/read_csv.md)
  reads a local delimited file into a base R `data.frame`, using a
  bundled subset of the [`zsv`](https://github.com/liquidaty/zsv) C
  parser. No R package dependencies.

- Handles quoted fields, embedded delimiters and newlines, escaped
  quotes, LF and CRLF, a missing final terminator, and a UTF-8 BOM.

- Infers `logical`, `integer`, `double` and `character` column types, or
  takes them from `col_types`. A value that will not convert to a forced
  type is an error naming the row, column and value, never a silent
  `NA`.

- `text` reads a CSV held in a character vector instead of a file, as
  `utils::read.csv(text=)` does — one element per line, so
  [`readLines()`](https://rdrr.io/r/base/readLines.html) output reads
  back unchanged. The string’s declared encoding is honoured, so a
  latin1 string reads correctly where a latin1 file is still an error.

- `header`, `delimiter` and `na` control the header row, the field
  delimiter (any single ASCII byte) and which cell values read as
  missing. `header = NA` detects whether the first record is names or
  data, from that record alone.

- [`sniff_csv()`](https://pedrobtz.github.io/zucsv/reference/sniff_csv.md)
  reports how a file would be read — header verdict, shape, column names
  and inferred types — without building the columns, so the decisions
  can be inspected and then pinned:
  `read_csv(f, header = s$header, col_types = s$col_types)` infers
  nothing.

- Documented, tested behavior that is easy to get wrong elsewhere: blank
  lines are skipped everywhere, every record must have the same width,
  column names are preserved exactly including duplicates, invalid UTF-8
  and embedded NULs are errors where they occur, and nothing is
  whitespace trimmed.

- Large reads are interruptible, and the file handle and parser are
  released on errors and interrupts alike.
