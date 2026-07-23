#pragma once

#include <stdarg.h>
#include <stdio.h>

#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_RESET "\x1b[0m"

namespace logger {
inline void log(const char* format, ...) {
  printf("Linux: ");

  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  printf("\n");
}

inline void warn(const char* format, ...) {
  printf(ANSI_COLOR_YELLOW "Linux: ");

  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  printf("\n" ANSI_COLOR_RESET);
}

inline void error(const char* format, ...) {
  printf(ANSI_COLOR_RED "Linux: ");

  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  printf("\n" ANSI_COLOR_RESET);
}
}  // namespace logger
