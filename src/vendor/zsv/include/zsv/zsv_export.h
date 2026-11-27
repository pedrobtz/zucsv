#ifndef ZSV_EXPORT_H
#define ZSV_EXPORT_H

// clang-format off
/* zucsv: a definition from the build wins, so -DZSV_EXPORT= leaves zsv's
 * functions to the build's visibility (-fvisibility=hidden). */
#ifdef ZSV_EXPORT
#elif defined(__EMSCRIPTEN__)
# include <emscripten.h>
# define ZSV_EXPORT EMSCRIPTEN_KEEPALIVE
#else
# ifdef ZSV_BUILD_SHARED
#  if defined(_WIN32) || defined(__CYGWIN__)
#   ifdef __GNUC__
#    define ZSV_EXPORT __attribute__((dllexport))
#   else
#    define ZSV_EXPORT __declspec(dllexport)
#   endif
#  else
#   ifdef __GNUC__
#    define ZSV_EXPORT __attribute__((visibility("default")))
#   else
#    define ZSV_EXPORT __declspec(dllimport)
#   endif
#  endif
# else
#  if __GNUC__ >= 4
#   define ZSV_EXPORT __attribute__((visibility("default")))
#  else
#   define ZSV_EXPORT
#  endif
# endif
#endif
// clang-format on

#endif
