#define R_NO_REMAP
#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>
#include <R_ext/Visibility.h>

#include "zucsv.h"

static const R_CallMethodDef call_methods[] = {
    {"C_zsv_smoke", (DL_FUNC)&C_zsv_smoke, 1},
    {NULL, NULL, 0}
};

attribute_visible void R_init_zucsv(DllInfo *dll) {
    R_registerRoutines(dll, NULL, call_methods, NULL, NULL);
    R_useDynamicSymbols(dll, FALSE);
    R_forceSymbols(dll, TRUE);
}
