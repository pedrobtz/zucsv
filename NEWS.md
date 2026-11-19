# zucsv 0.1.1

## Bug fixes

Several inputs read as a wrong table with no error. Each is now read
correctly or reported:

* A record longer than the 8,388,607-byte limit is always an error naming
  the record. Before, only an overflow inside the record's first cell was
  caught. An overflow in a later cell read as a short row followed by an
  invented one, made the record vanish, or reported an embedded NUL the file
  did not have (#28).

* A quote that is never closed is an error naming the row and column where
  it opened. Before, a quote opened in the last column took the rest of the
  file into one cell (#30). In an earlier column it gave a row-width error,
  which now names the real cause.

* A quoted last cell holding an escaped quote (`""`) reads correctly when the
  file has no final newline. Before, it kept its closing quote:
  `"x"""` read as `x""` instead of `x"` (#45).

* `text =` keeps non-ASCII characters in a session whose locale is not
  UTF-8, `readLines()` output included. Before, in a C locale, `"\u00e9"` read
  as the characters `<c3><a9>` or `<e9>`, with no error. Unmarked strings
  are now taken as UTF-8 bytes there, as a file's bytes are, so bytes that are
  not UTF-8 are an error (#57, #60).

* A one-byte delimiter that is not ASCII, such as a latin1 `"\xe9"`, is
  rejected with "`delimiter` must be a single ASCII byte." instead of R's
  "missing value where TRUE/FALSE needed" (#48).

## Performance

* Files with long cells read in linear time. The bundled parser rescanned
  the run before every token, so 50 rows of 64 KiB cells took 4.5 s; they
  now take 0.02 s (#31). A long read also checks for an interrupt at least
  every 64 MiB, not only every 16,384 records.

## Documentation

* `?read_csv` states the record and column limits (#28).

* `?read_csv` no longer claims that a CR LF pair inside a quoted field is
  normalised to LF. It never was: bytes inside a quoted field are kept as
  they are, CR included (#24). Read through `text = readLines(path)`, such a
  CR comes back as LF, and `?read_csv` now says so (#42).

# zucsv 0.1.0

First release.

* `read_csv()` reads a local delimited file into a base R `data.frame`, using
  a bundled subset of the [`zsv`](https://github.com/liquidaty/zsv) C parser.
  No R package dependencies.

* Handles quoted fields, embedded delimiters and newlines, escaped quotes,
  LF and CRLF, a missing final terminator, and a UTF-8 BOM.

* Infers `logical`, `integer`, `double` and `character` column types, or
  takes them from `col_types`. A value that will not convert to a forced type
  is an error naming the row, column and value, never a silent `NA`.

* `text` reads a CSV held in a character vector instead of a file, as
  `utils::read.csv(text=)` does --- one element per line, so `readLines()`
  output reads back unchanged. The string's declared encoding is honoured, so
  a latin1 string reads correctly where a latin1 file is still an error.

* `header`, `delimiter` and `na` control the header row, the field delimiter
  (any single ASCII byte) and which cell values read as missing. `header = NA`
  detects whether the first record is names or data, from that record alone.

* `sniff_csv()` reports how a file would be read --- header verdict, shape,
  column names and inferred types --- without building the columns, so the
  decisions can be inspected and then pinned:
  `read_csv(f, header = s$header, col_types = s$col_types)` infers nothing.

* Documented, tested behavior that is easy to get wrong elsewhere: blank
  lines are skipped everywhere, every record must have the same width, column
  names are preserved exactly including duplicates, invalid UTF-8 and
  embedded NULs are errors where they occur, and nothing is whitespace
  trimmed.

* Large reads are interruptible, and the file handle and parser are released
  on errors and interrupts alike.
