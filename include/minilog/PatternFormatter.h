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

#ifndef MINILOG_PATTERN_FORMATTER_H
#define MINILOG_PATTERN_FORMATTER_H

#include <minilog/Formatter.h>
#include <minilog/common.h>

#include "private/minilog/Message.h"
#include "private/minilog/noncopyable.h"
#include "private/minilog/Utils.h"

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace minilog {
namespace details {

/**
 * Padding information
 */
struct PaddingInfo {
  enum class Alignment : uint8_t {
    kLeft = 0,
    kRight = 1,
    kCenter = 2,
  };

  PaddingInfo() = default;

  PaddingInfo(const size_t width, const Alignment alignment, const bool truncate)
    : width(width),
      alignment(alignment),
      truncate(truncate),
      enabled(true) {
  }

  size_t width = 0;
  Alignment alignment = Alignment::kLeft;
  bool truncate = false;
  bool enabled = false;
};

/**
 * The Printer of the pattern format.
 */
class Printer {
public:
  virtual ~Printer() = default;

  Printer() = default;

  explicit Printer(const PaddingInfo paddingInfo) noexcept
    : mPaddingInfo(paddingInfo) {
  }

  virtual void format(const Message& msg, const std::tm& time, std::string& dest) = 0;

protected:
  PaddingInfo mPaddingInfo;
};

} // namespace details

class CustomPrinter : public noncopyable, public details::Printer {
public:
  void setPaddingInfo(const details::PaddingInfo& info) {
    mPaddingInfo = info;
  }

  [[nodiscard]] virtual std::unique_ptr<CustomPrinter> clone() const = 0;
};

class PatternFormatter final : public Formatter {
public:
  using CustomPrinters = std::unordered_map<char, std::shared_ptr<CustomPrinter>>;

  explicit PatternFormatter(std::string pattern, TimeZone timeZone = TimeZone::kLocal,
    std::string eol = Utils::defaultEol(), CustomPrinters userPrinters = CustomPrinters());

  explicit PatternFormatter(TimeZone timeZone = TimeZone::kLocal, std::string eol = Utils::defaultEol());

  [[nodiscard]] std::unique_ptr<Formatter> clone() const override;

  void format(const Message& msg, std::string& dest) override;

  void setPattern(std::string pattern);

  void needLocaltime(const bool need = true) { mNeedLocaltime = need; }

private:
  std::string mPattern;
  std::string mEol;
  TimeZone mTimeZone;
  bool mNeedLocaltime;
  std::tm mCachedTime{};
  // Seconds of the message the cached time belongs to. It starts at the smallest value instead of zero: zero is
  // the timestamp of the epoch, so a formatter that is given a message at 1970-01-01T00:00:00Z would otherwise
  // consider the zero-initialised cache valid and print the all-zero std::tm.
  std::chrono::seconds mLastLogSeconds{ std::chrono::seconds::min() };
  std::vector<std::unique_ptr<details::Printer>> mPrinters;
  CustomPrinters mCustomPrinters;

  [[nodiscard]] std::tm getTime(const Message& msg) const;

  template<typename Alignment>
  void handleFlag(char flag, details::PaddingInfo padding);

  static details::PaddingInfo
  handlePaddingInfo(std::string::const_iterator& it, const std::string::const_iterator& end);

  void compilePattern(const std::string& pattern);
};

} // namespace minilog

#endif // MINILOG_PATTERN_FORMATTER_H
