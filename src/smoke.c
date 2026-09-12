/* Stage 1 only: prove the vendored zsv parser builds, links and runs from R
   on every target platform. Stage 2 replaces this with the real reader. */
#include "zucsv.h"
#include "zsv.h"

/* Keeps vendored code from ever writing to the R session's stderr. */
static int zucsv_no_errprintf(void *ctx, const char *fmt, ...) {
  (void)ctx;
  (void)fmt;
  return 0;
}

SEXP C_zsv_smoke(SEXP file) {
  const char *path = Rf_translateChar(STRING_ELT(file, 0));
  FILE *f = fopen(path, "rb");
  if (!f)
    Rf_error("Cannot open CSV file: %s", path);

  struct zsv_opts opts;
  memset(&opts, 0, sizeof(opts));
  opts.stream = f;
  opts.max_columns = 1024;
  opts.errprintf = zucsv_no_errprintf;

  zsv_parser parser = zsv_new(&opts);
  if (!parser) {
    fclose(f);
    Rf_error("Cannot create CSV parser");
  }

  double records = 0, cells = 0;
  while (zsv_next_row(parser) == zsv_status_row) {
    records += 1;
    cells += (double)zsv_cell_count(parser);
  }

  zsv_finish(parser);
  zsv_delete(parser);
  fclose(f);

  SEXP out = PROTECT(Rf_allocVector(REALSXP, 2));
  REAL(out)[0] = records;
  REAL(out)[1] = cells;
  SEXP nms = PROTECT(Rf_allocVector(STRSXP, 2));
  SET_STRING_ELT(nms, 0, Rf_mkChar("records"));
  SET_STRING_ELT(nms, 1, Rf_mkChar("cells"));
  Rf_setAttrib(out, R_NamesSymbol, nms);
  UNPROTECT(2);
  return out;
}
