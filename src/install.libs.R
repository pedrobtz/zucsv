## Installs the shared object, and beside it the LinkingTo surface of
## design section 30 (#23): libzucsv.a under lib${R_ARCH}. The headers,
## zucsv.h and zucsv-r.h, are in inst/include, which R copies itself.
##
## Defining this file makes R stop installing the shared object by itself,
## so the first block is not optional: without it the package installs with
## no compiled code at all (Writing R Extensions 1.2.1.1).

zucsv_install <- function(from, to, what) {
  dir.create(to, recursive = TRUE, showWarnings = FALSE)
  ## file.copy() returns a logical per source and never signals, so an
  ## unchecked copy is how a package installs with a piece missing.
  ok <- file.copy(from, to, overwrite = TRUE)
  if (length(ok) == 0L || !all(ok)) {
    stop("zucsv: failed to install ", what, " into ", to)
  }
  invisible(TRUE)
}

libs <- file.path(R_PACKAGE_DIR, paste0("libs", R_ARCH))
zucsv_install(Sys.glob(paste0("*", SHLIB_EXT)), libs, "the shared object")
if (file.exists("symbols.rds")) {
  zucsv_install("symbols.rds", libs, "symbols.rds")
}

## The archive is object code for this architecture, so it goes under
## R_ARCH as the shared object does: <pkg>/lib on Unix, <pkg>/lib/x64 on
## Windows. A consumer finds it with system.file("lib", .Platform$r_arch,
## package = "zucsv"), falling back to system.file("lib", ...).
zucsv_install(
  "libzucsv.a",
  file.path(R_PACKAGE_DIR, paste0("lib", R_ARCH)),
  "libzucsv.a (src/Makevars should have built it)"
)
