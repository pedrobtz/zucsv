# zucsv: Read CSV Files Using the 'zsv' Parser

Reads comma-separated and other delimited text files into base R data
frames using the 'zsv' C parser <https://github.com/liquidaty/zsv> as
its native backend. Handles quoted fields, embedded delimiters and
newlines, escaped quotes, and both LF and CRLF line endings, and infers
logical, integer, double, and character column types. Column types can
also be given explicitly, in which case a value that cannot be converted
is an error rather than a silently missing value. The 'zsv' sources are
bundled, so there are no runtime dependencies.

## See also

Useful links:

- <https://github.com/pedrobtz/zucsv>

- Report bugs at <https://github.com/pedrobtz/zucsv/issues>

## Author

**Maintainer**: Pedro Baltazar <pedrobtz@gmail.com> \[copyright holder\]

Authors:

- Pedro Baltazar <pedrobtz@gmail.com> \[copyright holder\]

Other contributors:

- Tai Chi Minh Ralph Eastwood (Author of the bundled 'zsv' library)
  \[contributor, copyright holder\]

- Matt Wong (Author of the bundled 'zsv' library) \[contributor\]

- Guarnerix Inc dba Liquidaty (Copyright holder of the bundled 'zsv'
  library) \[copyright holder\]

- zsv contributors (Contributors to the bundled 'zsv' library)
  \[contributor, copyright holder\]

- Max Ogden (Author of the 'csv-spectrum' test fixtures) \[contributor,
  copyright holder\]
