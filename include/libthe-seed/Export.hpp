#pragma once

// Marks a class as part of the library's public interface. The library is
// compiled with hidden visibility, so only marked classes are exported from
// the shared library (and from the DLL on Windows). Applications and plugins
// see an empty macro except on non-Windows, where it is the default
// visibility attribute, which is harmless.
#if defined(_WIN32)
#  if defined(THE_SEED_BUILDING) && defined(DLL_EXPORT)
#    define THE_SEED_API __declspec(dllexport)
#  else
#    define THE_SEED_API
#  endif
#else
#  define THE_SEED_API __attribute__((visibility("default")))
#endif
