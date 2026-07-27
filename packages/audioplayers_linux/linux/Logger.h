#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_RESET "\x1b[0m"

#define LOG_GENERAL(color, format) \
  do {                             \
    printf(color "Linux: ");       \
    va_list args;                  \
    va_start(args, format);        \
    vprintf(format, args);         \
    va_end(args);                  \
    printf("\n" ANSI_COLOR_RESET); \
  } while (0)

class Logger {
  bool enabled = false;

 public:
  Logger() {
    const char* env_p = getenv("IS_LINUX_LOG");

    if (env_p != nullptr) {
      if (std::string(env_p) == "1") {
        enabled = true;
      }
    }
  }

  void log(const char* format, ...) {
    if (!enabled)
      return;

    LOG_GENERAL("", format);
  }

  void green(const char* format, ...) {
    if (!enabled)
      return;

    LOG_GENERAL(ANSI_COLOR_GREEN, format);
  }

  void blue(const char* format, ...) {
    if (!enabled)
      return;

    LOG_GENERAL(ANSI_COLOR_BLUE, format);
  }

  void warn(const char* format, ...) {
    if (!enabled)
      return;

    LOG_GENERAL(ANSI_COLOR_YELLOW, format);
  }

  void error(const char* format, ...) {
    if (!enabled)
      return;

    LOG_GENERAL(ANSI_COLOR_RED, format);
  }
};
