# The R face of the archive consumer. Nothing here calls zucsv: see
# NAMESPACE for why that is the point.

# zucsv_open_stream() on a path, run on a thread of its own (threaded =
# TRUE) or on R's: list(code, message, names, formats, batches, columns),
# the columns as R vectors of all batches together.
read_stream <- function(path, header = TRUE, delimiter = ",",
                        na = c("", "NA"), col_types = NULL,
                        batch_rows = 65536, threaded = TRUE) {
  .Call(
    C_read_stream, path.expand(path), header, delimiter, na, col_types,
    as.double(batch_rows), threaded
  )
}

# c(header, archive): ZUCSV_ABI_VERSION as compiled and as linked.
abi <- function() .Call(C_abi)

# The mode zucsv_strtod_probe() chose at load.
strtod_mode <- function() .Call(C_strtod_mode)
