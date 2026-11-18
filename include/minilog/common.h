/*
 * Copyright (C) 2026 The Coral Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MINILOG_COMMON_H
#define MINILOG_COMMON_H

#include <chrono>
#include <exception>

#include <version>
#if defined(__cpp_lib_format) && __cpp_lib_format >= 202207L
#include <format>
#endif

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

// clang-format off
#define MINILOG_LEVEL_TRACE    0
#define MINILOG_LEVEL_DEBUG    1
#define MINILOG_LEVEL_INFO     2
#define MINILOG_LEVEL_WARN     3
#define MINILOG_LEVEL_ERROR    4
#define MINILOG_LEVEL_CRITICAL 5
#define MINILOG_LEVEL_OFF      6

#define MINILOG_LEVEL_NAME_TRACE    std::string_view("trace", 5)
#define MINILOG_LEVEL_NAME_DEBUG    std::string_view("debug", 5)
#define MINILOG_LEVEL_NAME_INFO     std::string_view("info", 4)
#define MINILOG_LEVEL_NAME_WARN     std::string_view("warning", 7)
#define MINILOG_LEVEL_NAME_ERROR    std::string_view("error", 5)
#define MINILOG_LEVEL_NAME_CRITICAL std::string_view("critical", 8)
#define MINILOG_LEVEL_NAME_OFF      std::string_view("off", 3)
// clang-format on

#if !defined(MINILOG_ACTIVE_LEVEL)
#define MINILOG_ACTIVE_LEVEL MINILOG_LEVEL_INFO
#endif

// C++ exceptions are reported by a different macro on every compiler: __cpp_exceptions and __EXCEPTIONS on
// GCC and Clang, _CPPUNWIND on MSVC. Without this check the error path of the library aborts the process
// instead of throwing on MSVC.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define MINILOG_EXCEPTIONS_ENABLED 1
#else
#define MINILOG_EXCEPTIONS_ENABLED 0
#endif

#if MINILOG_EXCEPTIONS_ENABLED

#define MINILOG_THROW(ex) throw ex

#else

#define MINILOG_THROW(ex)                            \
  do {                                               \
    printf("minilog fatal error: %s\n", ex.what());  \
    std::abort();                                    \
  } while (0)

#endif

#if !defined(MINILOG_EOL)
#ifdef _WIN32
#define MINILOG_EOL "\r\n"
#else
#define MINILOG_EOL "\n"
#endif
#endif // MINILOG_EOL

#if !defined(MINILOG_SEPARATOR)
#ifdef _WIN32
#define MINILOG_SEPARATOR "\\/"
#else
#define MINILOG_SEPARATOR "/"
#endif
#endif // MINILOG_SEPARATOR

namespace minilog {

using Clock = std::chrono::system_clock;
using ErrorHandler = std::function<void(const std::string& err)>;


template<typename... Args>
#if defined(__cpp_lib_format) && __cpp_lib_format >= 202207L
using format_string_t = std::format_string<Args...>;
#else
using format_string_t = std::string_view;
#endif

template<class T, class Char = char>
struct is_convertible_to_basic_format_string
  : std::integral_constant<bool, std::is_convertible<T, std::basic_string_view<Char>>::value> {
};

enum Level : uint8_t {
  // clang-format off
  kTrace =    MINILOG_LEVEL_TRACE,
  kDebug =    MINILOG_LEVEL_DEBUG,
  kInfo =     MINILOG_LEVEL_INFO,
  kWarn =     MINILOG_LEVEL_WARN,
  kError =    MINILOG_LEVEL_ERROR,
  kCritical = MINILOG_LEVEL_CRITICAL,
  kOff =      MINILOG_LEVEL_OFF,
  // clang-format on

  kLevels = kOff + 1,
};

/**
 * The color mode used by sink with color support.
 */
enum class ColorMode : uint8_t {
  kAlways,
  kAutomatic,
  kNever,
};

/**
 * Specific time getting to use for pattern formatter.
 *
 * local time by default.
 */
enum class TimeZone : uint8_t {
  // clang-format off
  kLocal,  // log localtime
  kUTC,    // log utc
  // clang-format on
};

struct Source {
  const char* filename{ nullptr };
  int line{ 0 };
  const char* function{ nullptr };

  Source() = default;

  explicit Source(const char* filename, const int line, const char* function)
    : filename(filename),
      line(line),
      function(function) {
  }

  [[nodiscard]] constexpr bool empty() const { return line <= 0; }
};

class MiniLogException : public std::exception {
public:
  ~MiniLogException() override;

  explicit MiniLogException(std::string msg);

  explicit MiniLogException(const std::string& message, int last_errno) noexcept;

  [[nodiscard]] const char* what() const noexcept override {
    return mMessage.c_str();
  }

private:
  std::string mMessage;
};

} // namespace minilog

#endif // MINILOG_COMMON_H
