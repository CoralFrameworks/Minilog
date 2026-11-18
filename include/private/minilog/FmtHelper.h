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

#ifndef MINILOG_FMT_HELPER_H
#define MINILOG_FMT_HELPER_H

#include <minilog/common.h>

#include <charconv>
#include <limits>

namespace minilog {

class FmtHelper {
public:
  static void appendString(const std::string_view view, std::string& dest) {
    auto* buf = view.data();
    dest.append(buf, buf + view.size());
  }

  template<typename T>
  static void appendInt(T n, std::string& dest) {
    const auto BUF_SIZE = std::numeric_limits<T>::digits10 + 2;
    char buf[BUF_SIZE];
    auto [ptr, ec] = std::to_chars(buf, buf + BUF_SIZE, n, 10);
    if (ec == std::errc()) {
      dest.append(buf, ptr);
    } else {
      // if exceptions are enabled, throw now.
      MINILOG_THROW(MiniLogException("Failed to format int", static_cast<int>(ec)));
    }
  }

  template<typename T>
  static unsigned int countDigitsFallback(T n) {
    // taken from fmt: https://github.com/fmtlib/fmt/blob/8.0.1/include/fmt/format.h#L899-L912
    unsigned int count = 1;
    for (;;) {
      // Integer division is slow so do it for a group of four digits instead
      // of for every digit. The idea comes from the talk by Alexandrescu
      // "Three Optimization Tips for C++". See speed-test for a comparison.
      if (n < 10) return count;
      if (n < 100) return count + 1;
      if (n < 1000) return count + 2;
      if (n < 10000) return count + 3;
      n /= 10000u;
      count += 4;
    }
  }

  template<typename T>
  static unsigned int countDigits(T n) {
    using count_type = typename std::conditional<(sizeof(T) > sizeof(uint32_t)), uint64_t, uint32_t>::type;
    return countDigitsFallback(n);
  }

  template<typename T>
  static void appendUint(T n, unsigned int width, std::string& dest) {
    static_assert(std::is_unsigned<T>::value, "appendUint must get unsigned T");
    for (auto digits = countDigits(n); digits < width; ++digits) {
      dest.push_back('0');
    }
    appendInt(n, dest);
  }

  static void append2(int n, std::string& dest) {
    if (n >= 0 && n < 100) {
      dest.push_back(static_cast<char>('0' + n / 10));
      dest.push_back(static_cast<char>('0' + n % 10));
    } else {
      std::format_to(std::back_inserter(dest), "{:02}", n);
    }
  }

  template<typename T>
  static void append3(T n, std::string& dest) {
    static_assert(std::is_unsigned<T>::value, "append3 must get unsigned T");
    if (n < 1000) {
      dest.push_back(static_cast<char>(n / 100 + '0'));
      n = n % 100;
      dest.push_back(static_cast<char>((n / 10) + '0'));
      dest.push_back(static_cast<char>((n % 10) + '0'));
    } else {
      appendInt(n, dest);
    }
  }

  template<typename T>
  static void append6(T n, std::string& dest) {
    appendUint(n, 6, dest);
  }

  template<typename T>
  static void append9(T n, std::string& dest) {
    appendUint(n, 9, dest);
  }

  template<typename ToDuration>
  static ToDuration timeFraction(const Clock::time_point tp) {
    using std::chrono::duration_cast;
    using std::chrono::seconds;
    const auto duration = tp.time_since_epoch();
    const auto secs = duration_cast<seconds>(duration);
    return duration_cast<ToDuration>(duration) - duration_cast<ToDuration>(secs);
  }

private:
  FmtHelper() = default;
  ~FmtHelper() = default;
};

} // namespace minilog

#endif // MINILOG_FMT_HELPER_H
