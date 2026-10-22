// miniz_export.h - Simple export macros for miniz
// This is a minimal replacement for the missing miniz_export.h

#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

#if defined(_WIN32) && defined(MINIZ_DLL)
  #ifdef MINIZ_EXPORTS
    #define MINIZ_EXPORT __declspec(dllexport)
  #else
    #define MINIZ_EXPORT __declspec(dllimport)
  #endif
#else
  #define MINIZ_EXPORT
#endif

#endif // MINIZ_EXPORT_H
