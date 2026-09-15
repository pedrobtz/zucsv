#define R_NO_REMAP
#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>
#include <R_ext/Visibility.h>

#include "zucsv.h"

static const R_CallMethodDef call_methods[] = {
    {"C_read_csv", (DL_FUNC)&C_read_csv, 5},
    {"C_sniff_csv", (DL_FUNC)&C_sniff_csv, 3},
    {NULL, NULL, 0}
};

attribute_visible void R_init_zucsv(DllInfo *dll) {
    R_registerRoutines(dll, NULL, call_methods, NULL, NULL);
    zucsv_numeric_init();
    R_useDynamicSymbols(dll, FALSE);
    R_forceSymbols(dll, TRUE);
}
