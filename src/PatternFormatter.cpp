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

#include "minilog/PatternFormatter.h"

#include "private/minilog/FmtHelper.h"
#include "private/minilog/Utils.h"
#include "private/minilog/mdc.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>

namespace minilog {
namespace details {

class ScopedAlignment {
public:
  ScopedAlignment(const size_t wrappedSize, const PaddingInfo& padding, std::string& dest)
    : mPaddingInfo(padding),
      mDest(dest) {

    remainingAppend = static_cast<long>(padding.width) - static_cast<long>(wrappedSize);
    if (remainingAppend <= 0) {
      return;
    }

    if (padding.alignment == PaddingInfo::Alignment::kLeft) {
      appendIt(remainingAppend);
      remainingAppend = 0;
    } else if (padding.alignment == PaddingInfo::Alignment::kCenter) {
      const auto halfAppend = remainingAppend / 2;
      const auto reminder = remainingAppend & 1;
      appendIt(halfAppend);
      remainingAppend = halfAppend + reminder; // for the right side
    }
  }

  ~ScopedAlignment() {
    if (remainingAppend >= 0) {
      appendIt(remainingAppend);
    } else if (mPaddingInfo.truncate) {
      auto newSize = static_cast<long>(mDest.size()) + remainingAppend;
      if (newSize < 0) {
        newSize = 0;
      }
      mDest.resize(static_cast<size_t>(newSize));
    }
  }

  template<typename T>
  static unsigned int count_digits(T n) {
    return FmtHelper::countDigits(n);
  }

private:
  void appendIt(const long count) const {
    FmtHelper::appendString(std::string_view(spaces.data(), static_cast<size_t>(count)), mDest);
  }

  const PaddingInfo& mPaddingInfo;
  std::string& mDest;
  long remainingAppend;
  std::string_view spaces{ "                                                                ", 64 };
};

struct NullAlignment {
  NullAlignment(size_t /*wrappedSize*/, const PaddingInfo& /* padding*/, std::string& /*dest*/) {
  }

  template<typename T>
  static unsigned int count_digits(T /*n*/) {
    return 0;
  }
};

//----------------------------------------------------------------------------------------------------------------------

/**
 * Print name
 */
template<typename Alignment>
class NamePrinter final : public Printer {
public:
  explicit NamePrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    Alignment p(msg.name.size(), mPaddingInfo, dest);
    FmtHelper::appendString(msg.name, dest);
  }
};

static constexpr std::string_view LevelName[] = {
  // clang-format off
  MINILOG_LEVEL_NAME_TRACE,
  MINILOG_LEVEL_NAME_DEBUG,
  MINILOG_LEVEL_NAME_INFO,
  MINILOG_LEVEL_NAME_WARN,
  MINILOG_LEVEL_NAME_ERROR,
  MINILOG_LEVEL_NAME_CRITICAL,
  MINILOG_LEVEL_NAME_OFF
  // clang-format on
};

/**
 * Print level name
 */
template<typename Alignment>
class LevelPrinter final : public Printer {
public:
  explicit LevelPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    const std::string_view& levelName = LevelName[msg.level];
    Alignment p(levelName.size(), mPaddingInfo, dest);
    FmtHelper::appendString(levelName, dest);
  }

private:
};

/**
 * Print short name of Level
 */
template<typename Alignment>
class LevelShortNamePrinter final : public Printer {
public:
  explicit LevelShortNamePrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    const std::string_view levelName{ ShortLevelName[msg.level] };
    Alignment p(levelName.size(), mPaddingInfo, dest);
    FmtHelper::appendString(levelName, dest);
  }

private:
  static constexpr std::array<const char*, 7> ShortLevelName = {
    "T", "D", "I", "W", "E", "C", "O"
  };
};

static const char* ampm(const tm& t) { return t.tm_hour >= 12 ? "PM" : "AM"; }

// 12-hour clock: midnight is 12 AM, noon is 12 PM, 13:00 is 01 PM. The previous implementation returned 0 for
// midnight although the flag is documented as the range [1, 12].
static int to12h(const tm& t) { return t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12; }

// Abbreviated weekday name
static constexpr std::array<const char*, 7> WeekdayName{
  { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" }
};

/**
 * Print weekday name.
 */
template<typename Alignment>
class WeekdayNamePrinter final : public Printer {
public:
  explicit WeekdayNamePrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    const std::string_view fieldValue{ WeekdayName[static_cast<size_t>(time.tm_wday)] };
    Alignment p(fieldValue.size(), mPaddingInfo, dest);
    FmtHelper::appendString(fieldValue, dest);
  }
};

/**
 * Print full name of weekday
 */
template<typename Alignment>
class WeekdayFullNamePrinter final : public Printer {
public:
  explicit WeekdayFullNamePrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    const std::string_view fieldValue{ WeekDayFullName[static_cast<size_t>(time.tm_wday)] };
    Alignment p(fieldValue.size(), mPaddingInfo, dest);
    FmtHelper::appendString(fieldValue, dest);
  }

private:
  static constexpr std::array<const char*, 7> WeekDayFullName{
    { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" }
  };
};

// Abbreviated month
static constexpr std::array<const char*, 12> MonthsName{
  { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" }
};

/**
 * Print name of month.
 */
template<typename Alignment>
class MonthNamePrinter final : public Printer {
public:
  explicit MonthNamePrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    const std::string_view monthStr{ MonthsName[static_cast<size_t>(time.tm_mon)] };
    Alignment p(monthStr.size(), mPaddingInfo, dest);
    FmtHelper::appendString(monthStr, dest);
  }
};

static constexpr std::array<const char*, 12> MonthsFullName{
  // clang-format off
  {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December"
  }
  // clang-format on
};

/**
 * Print full name of month.
 */
template<typename Alignment>
class MonthFullNamePrinter final : public Printer {
public:
  explicit MonthFullNamePrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    const std::string_view monthStr{ MonthsFullName[static_cast<size_t>(time.tm_mon)] };
    Alignment p(monthStr.size(), mPaddingInfo, dest);
    FmtHelper::appendString(monthStr, dest);
  }
};

/**
 * Print date and time representation (Thu Aug 23 15:35:46 2014)
 */
template<typename Alignment>
class DateAndTimePrinter final : public Printer {
public:
  explicit DateAndTimePrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t fieldSize = 24;
    Alignment p(fieldSize, mPaddingInfo, dest);

    FmtHelper::appendString(WeekdayName[static_cast<size_t>(time.tm_wday)], dest);
    dest.push_back(' ');
    FmtHelper::appendString(MonthsName[static_cast<size_t>(time.tm_mon)], dest);
    dest.push_back(' ');
    FmtHelper::appendInt(time.tm_mday, dest);
    dest.push_back(' ');

    FmtHelper::append2(time.tm_hour, dest);
    dest.push_back(':');
    FmtHelper::append2(time.tm_min, dest);
    dest.push_back(':');
    FmtHelper::append2(time.tm_sec, dest);
    dest.push_back(' ');
    FmtHelper::appendInt(time.tm_year + 1900, dest);
  }
};

/**
 * Print year in 2 digits
 */
template<typename Alignment>
class YearPrinter final : public Printer {
public:
  explicit YearPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t fieldSize = 2;
    Alignment p(fieldSize, mPaddingInfo, dest);
    FmtHelper::append2(time.tm_year % 100, dest);
  }
};

/**
 * Print short MM/DD/YY date, equivalent to %m/%d/%y 08/23/01
 */
template<typename Alignment>
class ShortDatePrinter final : public Printer {
public:
  explicit ShortDatePrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t fieldSize = 8;
    Alignment p(fieldSize, mPaddingInfo, dest);

    FmtHelper::append2(time.tm_mon + 1, dest);
    dest.push_back('/');
    FmtHelper::append2(time.tm_mday, dest);
    dest.push_back('/');
    FmtHelper::append2(time.tm_year % 100, dest);
  }
};

/**
 * Print year using 4 digits
 */
template<typename Alignment>
class FullYearPrinter final : public Printer {
public:
  explicit FullYearPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t size = 4;
    Alignment p(size, mPaddingInfo, dest);
    FmtHelper::appendInt(time.tm_year + 1900, dest);
  }
};

/**
 * Print month in ranges: [1, 12]
 */
template<typename Alignment>
class MonthNumberPrinter final : public Printer {
public:
  explicit MonthNumberPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t size = 2;
    Alignment p(size, mPaddingInfo, dest);
    FmtHelper::append2(time.tm_mon + 1, dest);
  }
};

/**
 * Print Day in ranges: [1, 31]
 */
template<typename Alignment>
class DayNumberPrinter final : public Printer {
public:
  explicit DayNumberPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t size = 2;
    Alignment p(size, mPaddingInfo, dest);
    FmtHelper::append2(time.tm_mday, dest);
  }
};

/**
 * Print hours in ranges: [0, 23]
 */
template<typename Alignment>
class Hour24Printer final : public Printer {
public:
  explicit Hour24Printer(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t size = 2;
    Alignment p(size, mPaddingInfo, dest);
    FmtHelper::append2(time.tm_hour, dest);
  }
};

/**
 * Print hours in ranges: [1, 12]
 */
template<typename Alignment>
class Hour12Printer final : public Printer {
public:
  explicit Hour12Printer(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t size = 2;
    Alignment p(size, mPaddingInfo, dest);
    FmtHelper::append2(to12h(time), dest);
  }
};

/**
 * Print minutes in ranges: [0, 59]
 */
template<typename Alignment>
class MinutesPrinter final : public Printer {
public:
  explicit MinutesPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t size = 2;
    Alignment p(size, mPaddingInfo, dest);
    FmtHelper::append2(time.tm_min, dest);
  }
};

/**
 * Print seconds in ranges: [0, 59]
 */
template<typename Alignment>
class SecondsPrinter final : public Printer {
public:
  explicit SecondsPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t size = 2;
    Alignment p(size, mPaddingInfo, dest);
    FmtHelper::append2(time.tm_sec, dest);
  }
};

/**
 * Print milliseconds
 */
template<typename Alignment>
class MillisecondsPrinter final : public Printer {
public:
  explicit MillisecondsPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    const auto millis = FmtHelper::timeFraction<std::chrono::milliseconds>(msg.time);
    constexpr size_t size = 3;
    Alignment p(size, mPaddingInfo, dest);
    FmtHelper::append3(static_cast<uint32_t>(millis.count()), dest);
  }
};


/**
 * Print microseconds
 */
template<typename Alignment>
class MicrosecondsPrinter final : public Printer {
public:
  explicit MicrosecondsPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    const auto micros = FmtHelper::timeFraction<std::chrono::microseconds>(msg.time);

    constexpr size_t size = 6;
    Alignment p(size, mPaddingInfo, dest);
    FmtHelper::append6(static_cast<size_t>(micros.count()), dest);
  }
};

/**
 * Print nanoseconds
 */
template<typename Alignment>
class NanosecondsPrinter final : public Printer {
public:
  explicit NanosecondsPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    const auto ns = FmtHelper::timeFraction<std::chrono::nanoseconds>(msg.time);
    constexpr size_t field_size = 9;
    Alignment p(field_size, mPaddingInfo, dest);
    FmtHelper::append9(static_cast<size_t>(ns.count()), dest);
  }
};


/**
 * Print seconds since epoch
 */
template<typename Alignment>
class SecondSinceEpochPrinter final : public Printer {
public:
  explicit SecondSinceEpochPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t size = 10;
    Alignment p(size, mPaddingInfo, dest);
    const auto duration = msg.time.time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    FmtHelper::appendInt(seconds, dest);
  }
};

/**
 * Print ante meridiem or post meridiem
 */
template<typename Alignment>
class MeridiemPrinter final : public Printer {
public:
  explicit MeridiemPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t size = 2;
    Alignment p(size, mPaddingInfo, dest);
    FmtHelper::appendString(ampm(time), dest);
  }
};

/**
 * Print 12-hour clock 02:55:02 pm
 */
template<typename Alignment>
class MeridiemHourPrinter final : public Printer {
public:
  explicit MeridiemHourPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t size = 11;
    Alignment p(size, mPaddingInfo, dest);

    FmtHelper::append2(to12h(time), dest);
    dest.push_back(':');
    FmtHelper::append2(time.tm_min, dest);
    dest.push_back(':');
    FmtHelper::append2(time.tm_sec, dest);
    dest.push_back(' ');
    FmtHelper::appendString(ampm(time), dest);
  }
};

/**
 * Print 24-hour HH:MM time, equivalent to %H:%M
 */
template<typename Alignment>
class FullHourPrinter final : public Printer {
public:
  explicit FullHourPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t size = 5;
    Alignment p(size, mPaddingInfo, dest);

    FmtHelper::append2(time.tm_hour, dest);
    dest.push_back(':');
    FmtHelper::append2(time.tm_min, dest);
  }
};

/**
 * Print ISO 8601 time format (HH:MM:SS), equivalent to %H:%M:%S
 */
template<typename Alignment>
class IsoTimePrinter final : public Printer {
public:
  explicit IsoTimePrinter(const PaddingInfo& padding) noexcept
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t size = 8;
    Alignment p(size, mPaddingInfo, dest);

    FmtHelper::append2(time.tm_hour, dest);
    dest.push_back(':');
    FmtHelper::append2(time.tm_min, dest);
    dest.push_back(':');
    FmtHelper::append2(time.tm_sec, dest);
  }
};

/**
 * Print ISO 8601 offset from UTC in timezone (+-HH:MM)
 */
template<typename Alignment>
class UtcTimePrinter final : public Printer {
public:
  explicit UtcTimePrinter(const PaddingInfo& padding, const TimeZone timeSpecific)
    : Printer(padding),
      mTimeZone(timeSpecific) {
  }

  UtcTimePrinter() = default;

  UtcTimePrinter(const UtcTimePrinter&) = delete;
  UtcTimePrinter& operator=(const UtcTimePrinter&) = delete;

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    constexpr size_t size = 6;
    Alignment p(size, mPaddingInfo, dest);

    if (mTimeZone == TimeZone::kUTC) {
      auto zeroes = "+00:00";
      dest.append(zeroes, zeroes + 6);
      return;
    }

    auto totalMinutes = get_cached_offset(msg, time);
    if (totalMinutes < 0) {
      totalMinutes = -totalMinutes;
      dest.push_back('-');
    } else {
      dest.push_back('+');
    }

    FmtHelper::append2(totalMinutes / 60, dest); // hours
    dest.push_back(':');
    FmtHelper::append2(totalMinutes % 60, dest); // minutes
  }

private:
  Clock::time_point mLastUpdate{ std::chrono::seconds(0) };
  int mOffsetMinutes{ 0 };
  bool mOffsetValid{ false };

  int get_cached_offset(const Message& msg, const std::tm& tm_time) {
    // refresh every 10 seconds, and on the very first message - including one at the epoch, whose time is zero
    if (!mOffsetValid || msg.time - mLastUpdate >= std::chrono::seconds(10)) {
      mOffsetMinutes = Utils::utcMinutesOffset(tm_time);
      mLastUpdate = msg.time;
      mOffsetValid = true;
    }
    return mOffsetMinutes;
  }

  TimeZone mTimeZone{ TimeZone::kLocal };
};

/**
 * Print thread id
 */
template<typename Alignment>
class TidPrinter final : public Printer {
public:
  explicit TidPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    const auto field_size = Alignment::count_digits(msg.thread_id);
    Alignment p(field_size, mPaddingInfo, dest);
    FmtHelper::appendInt(msg.thread_id, dest);
  }
};

/**
 * Print current pid
 */
template<typename Alignment>
class PidPrinter final : public Printer {
public:
  explicit PidPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    const auto pid = static_cast<uint32_t>(Utils::pid());
    auto size = Alignment::count_digits(pid);
    Alignment p(size, mPaddingInfo, dest);
    FmtHelper::appendInt(pid, dest);
  }
};

/**
 * Print the payload of the Message
 */
template<typename Alignment>
class PayloadPrinter final : public Printer {
public:
  explicit PayloadPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    Alignment p(msg.payload.size(), mPaddingInfo, dest);
    FmtHelper::appendString(msg.payload, dest);
  }
};

/**
 * Print char.
 */
class CharPrinter final : public Printer {
public:
  explicit CharPrinter(const char ch)
    : value(ch) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    dest.push_back(static_cast<char>(value));
  }

private:
  char value;
};

/**
 * Print aggregate string from chars.
 */
class AggregatePrinter final : public Printer {
public:
  AggregatePrinter() {
  }

  void addChar(const char ch) { str += ch; }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    FmtHelper::appendString(str, dest);
  }

private:
  std::string str;
};

/**
 * mark the color range. expect it to be in the form of "%^colored text%$"
 */
class ColorRangeStartPrinter final : public Printer {
public:
  explicit ColorRangeStartPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    msg.colorRangeStart = dest.size();
  }
};

/**
 * Print color range end in message.
 */
class ColorRangeEndPrinter final : public Printer {
public:
  explicit ColorRangeEndPrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    msg.colorRangeEnd = dest.size();
  }
};

/**
 * Print source location:
 * "filename:line"
 */
template<typename Alignment>
class FileLinePrinter final : public Printer {
public:
  explicit FileLinePrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    if (msg.source.empty()) {
      Alignment p(0, mPaddingInfo, dest);
      return;
    }

    size_t textSize;
    if (mPaddingInfo.enabled) {
      // calc text size for padding based on "filename:line"
      textSize = std::char_traits<char>::length(msg.source.filename) + Alignment::count_digits(msg.source.line) + 1;
    } else {
      textSize = 0;
    }

    Alignment p(textSize, mPaddingInfo, dest);
    FmtHelper::appendString(msg.source.filename, dest);
    dest.push_back(':');
    FmtHelper::appendInt(msg.source.line, dest);
  }
};

/**
 * Print filename
 */
template<typename Alignment>
class FileNamePrinter final : public Printer {
public:
  explicit FileNamePrinter(const PaddingInfo& padding)
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    if (msg.source.empty()) {
      Alignment p(0, mPaddingInfo, dest);
      return;
    }
    size_t text_size = mPaddingInfo.enabled ? std::char_traits<char>::length(msg.source.filename) : 0;
    Alignment p(text_size, mPaddingInfo, dest);
    FmtHelper::appendString(msg.source.filename, dest);
  }
};

/**
 * Print canonical name of the File.
 */
template<typename Alignment>
class FileCanonicalPrinter final : public Printer {
public:
  explicit FileCanonicalPrinter(const PaddingInfo& padding) noexcept
    : Printer(padding) {
  }

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127) // consider using 'if constexpr' instead
#endif                          // _MSC_VER
  static const char* basename(const char* filename) {
    constexpr char folderSeparator[] = MINILOG_SEPARATOR;
    // if the size is 2 (1 character + null terminator) we can use the more efficient strrchr
    // the branch will be elided by optimizations
    if (sizeof(folderSeparator) == 2) {
      const char* rv = std::strrchr(filename, folderSeparator[0]);
      return rv != nullptr ? rv + 1 : filename;
    } else {
      const std::reverse_iterator<const char*> begin(filename + std::strlen(filename));
      const std::reverse_iterator<const char*> end(filename);

      const auto it =
        std::find_first_of(begin, end, std::begin(folderSeparator), std::end(folderSeparator) - 1);
      return it != end ? it.base() : filename;
    }
  }
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    if (msg.source.empty()) {
      Alignment p(0, mPaddingInfo, dest);
      return;
    }

    // %s is the short source name: the file name without the directory. Printing the line number here made
    // "%s:%#" repeat the line number instead of naming the file.
    const char* filename = basename(msg.source.filename);
    const auto textSize = mPaddingInfo.enabled ? std::char_traits<char>::length(filename) : 0;
    Alignment p(textSize, mPaddingInfo, dest);
    FmtHelper::appendString(filename, dest);
  }
};

/**
 * Print line number
 */
template<typename Alignment>
class SourceLineNumberPrinter final : public Printer {
public:
  explicit SourceLineNumberPrinter(const PaddingInfo& padding) noexcept
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    if (msg.source.empty()) {
      Alignment p(0, mPaddingInfo, dest);
      return;
    }
    auto fieldSize = Alignment::count_digits(msg.source.line);
    Alignment p(fieldSize, mPaddingInfo, dest);
    FmtHelper::appendInt(msg.source.line, dest);
  }
};

/**
 * Print the function name
 */
template<typename Alignment>
class SourceFunctionPrinter final : public Printer {
public:
  explicit SourceFunctionPrinter(const PaddingInfo& padding) noexcept
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    if (msg.source.empty()) {
      Alignment p(0, mPaddingInfo, dest);
      return;
    }
    size_t text_size = mPaddingInfo.enabled ? std::char_traits<char>::length(msg.source.function) : 0;
    Alignment p(text_size, mPaddingInfo, dest);
    FmtHelper::appendString(msg.source.function, dest);
  }
};

/**
 * Print elapsed time since last message
 */
template<typename Alignment, typename Units>
class ElapsedTimePrinter final : public Printer {
public:
  using DurationUnits = Units;

  explicit ElapsedTimePrinter(const PaddingInfo& padding) noexcept
    : Printer(padding),
      mLastMsgTime(Clock::now()) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    const auto delta = (std::max)(msg.time - mLastMsgTime, Clock::duration::zero());
    auto deltaUnits = std::chrono::duration_cast<DurationUnits>(delta);
    mLastMsgTime = msg.time;
    auto deltaCount = static_cast<size_t>(deltaUnits.count());
    auto n_digits = static_cast<size_t>(Alignment::count_digits(deltaCount));
    Alignment p(n_digits, mPaddingInfo, dest);
    FmtHelper::appendInt(deltaCount, dest);
  }

private:
  Clock::time_point mLastMsgTime;
};

#ifndef MINILOG_NO_TLS
template<typename Alignment>
class MdcPrinter : public Printer {
public:
  explicit MdcPrinter(const PaddingInfo& padding) noexcept
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& time, std::string& dest) override {
    if (const auto& context = mdc::getContext(); context.empty()) {
      Alignment p(0, mPaddingInfo, dest);
    } else {
      onFormat(context, dest);
    }
  }

  void onFormat(const mdc::map_t& map, std::string& dest) {
    const auto last_element = std::prev(map.end());
    for (auto it = map.begin(); it != map.end(); ++it) {
      const auto& key = it->first;
      const auto& value = it->second;
      size_t content_size = key.size() + value.size() + 1; // 1 for ':'

      if (it != last_element) {
        content_size++; // 1 for ' '
      }

      Alignment p(content_size, mPaddingInfo, dest);
      FmtHelper::appendString(key, dest);
      FmtHelper::appendString(":", dest);
      FmtHelper::appendString(value, dest);
      if (it != last_element) {
        FmtHelper::appendString(" ", dest);
      }
    }
  }
};
#endif

/**
 * Print all information:
 *
 * pattern format:
 * [%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [%s:%#] %v
 */
class FullPrinter final : public Printer {
public:
  explicit FullPrinter(const PaddingInfo& padding) noexcept
    : Printer(padding) {
  }

  void format(const Message& msg, const std::tm& tm_time, std::string& dest) override {
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    using std::chrono::seconds;

    // cache the date/time part for the next second.
    const auto secs = duration_cast<seconds>(msg.time.time_since_epoch());

    if (mCacheTimestamp != secs || mCachedDatetime.size() == 0) {
      mCachedDatetime.clear();
      mCachedDatetime.push_back('[');
      FmtHelper::appendInt(tm_time.tm_year + 1900, mCachedDatetime);
      mCachedDatetime.push_back('-');

      FmtHelper::append2(tm_time.tm_mon + 1, mCachedDatetime);
      mCachedDatetime.push_back('-');

      FmtHelper::append2(tm_time.tm_mday, mCachedDatetime);
      mCachedDatetime.push_back(' ');

      FmtHelper::append2(tm_time.tm_hour, mCachedDatetime);
      mCachedDatetime.push_back(':');

      FmtHelper::append2(tm_time.tm_min, mCachedDatetime);
      mCachedDatetime.push_back(':');

      FmtHelper::append2(tm_time.tm_sec, mCachedDatetime);
      mCachedDatetime.push_back('.');

      mCacheTimestamp = secs;
    }
    dest.append(mCachedDatetime.begin(), mCachedDatetime.end());

    auto millis = FmtHelper::timeFraction<milliseconds>(msg.time);
    FmtHelper::append3(static_cast<uint32_t>(millis.count()), dest);
    dest.push_back(']');
    dest.push_back(' ');

    // append logger name if exists
    if (msg.name.size() > 0) {
      dest.push_back('[');
      FmtHelper::appendString(msg.name, dest);
      dest.push_back(']');
      dest.push_back(' ');
    }

    dest.push_back('[');
    // wrap the level name with color
    msg.colorRangeStart = dest.size();
    // FmtHelper::appendString(level::to_c_str(msg.level), dest);
    FmtHelper::appendString(LevelName[msg.level], dest);
    msg.colorRangeEnd = dest.size();
    dest.push_back(']');
    dest.push_back(' ');

    // add source location if present
    if (!msg.source.empty()) {
      dest.push_back('[');
      const char* filename = FileCanonicalPrinter<NullAlignment>::basename(msg.source.filename);
      FmtHelper::appendString(filename, dest);
      dest.push_back(':');
      FmtHelper::appendInt(msg.source.line, dest);
      dest.push_back(']');
      dest.push_back(' ');
    }

#ifndef MINILOG_NO_TLS
    // add mdc if present
    mdc::map_t mdc_map = mdc::getContext();
    if (!mdc_map.empty()) {
      dest.push_back('[');
      mMdcPrinter.onFormat(mdc_map, dest);
      dest.push_back(']');
      dest.push_back(' ');
    }
#endif
    FmtHelper::appendString(msg.payload, dest);
  }

private:
  std::chrono::seconds mCacheTimestamp{ 0 };
  std::string mCachedDatetime;
#ifndef MINILOG_NO_TLS
  MdcPrinter<NullAlignment> mMdcPrinter{ PaddingInfo{} };
#endif
};

} // namespace details

PatternFormatter::PatternFormatter(std::string pattern, TimeZone timeZone, std::string eol, CustomPrinters userPrinters)
  : mPattern(std::move(pattern)),
    mEol(std::move(eol)),
    mTimeZone(timeZone),
    mNeedLocaltime(false),
    mCustomPrinters(std::move(userPrinters)) {
  std::memset(&mCachedTime, 0, sizeof(mCachedTime));
  // mPattern, not the parameter: the parameter was moved into mPattern by the initializer list and is left
  // empty, so compiling it would build a formatter that prints nothing but the end of line.
  compilePattern(mPattern);
}

PatternFormatter::PatternFormatter(const TimeZone timeZone, std::string eol)
  : mPattern("%+"),
    mEol(std::move(eol)),
    mTimeZone(timeZone),
    mNeedLocaltime(true) {
  std::memset(&mCachedTime, 0, sizeof(mCachedTime));
  mPrinters.push_back(std::make_unique<details::FullPrinter>(details::PaddingInfo()));
}

std::unique_ptr<Formatter> PatternFormatter::clone() const {
  CustomPrinters formatters;
  for (const auto& [fst, snd]: mCustomPrinters) {
    formatters[fst] = snd->clone();
  }
  auto cloned = std::make_unique<PatternFormatter>(mPattern, mTimeZone, mEol, std::move(formatters));
  cloned->needLocaltime(mNeedLocaltime);
#if defined(__GNUC__) && __GNUC__ < 5
  return std::move(cloned);
#else
  return cloned;
#endif
}

void PatternFormatter::format(const Message& msg, std::string& dest) {
  if (mNeedLocaltime) {
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(msg.time.time_since_epoch());
    if (secs != mLastLogSeconds) {
      mCachedTime = getTime(msg);
      mLastLogSeconds = secs;
    }
  }

  for (const auto& printer: mPrinters) {
    printer->format(msg, mCachedTime, dest);
  }

  const auto* buf_ptr = mEol.data();
  dest.append(buf_ptr, buf_ptr + mEol.size());
}

void PatternFormatter::setPattern(std::string pattern) {
  mPattern = std::move(pattern);
  needLocaltime(false);
  compilePattern(mPattern);
}

std::tm PatternFormatter::getTime(const Message& msg) const {
  const auto time_tt = Clock::to_time_t(msg.time);
  if (mTimeZone == TimeZone::kLocal) {
    return Utils::localtime(time_tt);
  }
  return Utils::gmtime(time_tt);
}

template<typename Alignment>
void PatternFormatter::handleFlag(const char flag, details::PaddingInfo padding) {
  // process custom flags
  const auto it = mCustomPrinters.find(flag);
  if (it != mCustomPrinters.end()) {
    auto custom_handler = it->second->clone();
    custom_handler->setPaddingInfo(padding);
    mPrinters.push_back(std::move(custom_handler));
    return;
  }

  // process built-in flags
  switch (flag) {
    case ('+'): {
      // default formatter
      mPrinters.push_back(std::make_unique<details::FullPrinter>(padding));
      mNeedLocaltime = true;
      break;
    }

    case 'n': {
      // logger name
      mPrinters.push_back(std::make_unique<details::NamePrinter<Alignment>>(padding));
      break;
    }

    case 'l': {
      // level
      mPrinters.push_back(std::make_unique<details::LevelPrinter<Alignment>>(padding));
      break;
    }

    case 'L': {
      // short level
      mPrinters.push_back(std::make_unique<details::LevelShortNamePrinter<Alignment>>(padding));
      break;
    }

    case ('t'): {
      // thread id
      mPrinters.push_back(std::make_unique<details::TidPrinter<Alignment>>(padding));
      break;
    }

    case ('v'): {
      // the message text
      mPrinters.push_back(std::make_unique<details::PayloadPrinter<Alignment>>(padding));
      break;
    }

    case ('a'): {
      // weekday
      mPrinters.push_back(std::make_unique<details::WeekdayNamePrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('A'): {
      // full weekday
      mPrinters.push_back(std::make_unique<details::WeekdayFullNamePrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('b'):
    case ('h'): {
      // month
      mPrinters.push_back(std::make_unique<details::MonthNamePrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('B'): {
      // short month
      mPrinters.push_back(std::make_unique<details::MonthFullNamePrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('c'): {
      // datetime
      mPrinters.push_back(std::make_unique<details::DateAndTimePrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('C'): {
      // year 2 digits
      mPrinters.push_back(std::make_unique<details::YearPrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('Y'): {
      // year 4 digits
      mPrinters.push_back(std::make_unique<details::FullYearPrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('D'):
    case ('x'): {
      // datetime MM/DD/YY
      mPrinters.push_back(std::make_unique<details::ShortDatePrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('m'): {
      // month 1-12
      mPrinters.push_back(std::make_unique<details::MonthNumberPrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('d'): {
      // day of month 1-31
      mPrinters.push_back(std::make_unique<details::DayNumberPrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('H'): {
      // hours 24
      mPrinters.push_back(std::make_unique<details::Hour24Printer<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('I'): {
      // hours 12
      mPrinters.push_back(std::make_unique<details::Hour12Printer<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('M'): {
      // minutes
      mPrinters.push_back(std::make_unique<details::MinutesPrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('S'): {
      // seconds
      mPrinters.push_back(std::make_unique<details::SecondsPrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('e'): {
      // milliseconds
      mPrinters.push_back(std::make_unique<details::MillisecondsPrinter<Alignment>>(padding));
      break;
    }

    case ('f'): {
      // microseconds
      mPrinters.push_back(std::make_unique<details::MicrosecondsPrinter<Alignment>>(padding));
      break;
    }

    case ('F'): {
      // nanoseconds
      mPrinters.push_back(std::make_unique<details::NanosecondsPrinter<Alignment>>(padding));
      break;
    }

    case ('E'): {
      // seconds since epoch
      mPrinters.push_back(std::make_unique<details::SecondSinceEpochPrinter<Alignment>>(padding));
      break;
    }

    case ('p'): {
      // am/pm
      mPrinters.push_back(std::make_unique<details::MeridiemPrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('r'): {
      // 12-hour clock 02:55:02 pm
      mPrinters.push_back(std::make_unique<details::MeridiemHourPrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('R'): {
      // 24-hour HH:MM time
      mPrinters.push_back(std::make_unique<details::FullHourPrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }

    case ('T'):
    case ('X'): {
      // ISO 8601 time format (HH:MM:SS)
      mPrinters.push_back(std::make_unique<details::IsoTimePrinter<Alignment>>(padding));
      mNeedLocaltime = true;
      break;
    }
    case ('z'): {
      // timezone
      mPrinters.push_back(std::make_unique<details::UtcTimePrinter<Alignment>>(padding, mTimeZone));
      mNeedLocaltime = true;
      break;
    }
    case ('P'): {
      // pid
      mPrinters.push_back(std::make_unique<details::PidPrinter<Alignment>>(padding));
      break;
    }

    case ('^'): {
      // color range start
      mPrinters.push_back(std::make_unique<details::ColorRangeStartPrinter>(padding));
      break;
    }

    case ('$'): {
      // color range end
      mPrinters.push_back(std::make_unique<details::ColorRangeEndPrinter>(padding));
      break;
    }

    case ('@'): {
      // source location (filename:line)
      mPrinters.push_back(std::make_unique<details::FileLinePrinter<Alignment>>(padding));
      break;
    }

    case ('s'): {
      // short source filename - without directory name
      mPrinters.push_back(std::make_unique<details::FileCanonicalPrinter<Alignment>>(padding));
      break;
    }

    case ('g'): {
      // full source filename
      mPrinters.push_back(std::make_unique<details::FileNamePrinter<Alignment>>(padding));
      break;
    }

    case ('#'): {
      // source line number
      mPrinters.push_back(std::make_unique<details::SourceLineNumberPrinter<Alignment>>(padding));
      break;
    }

    case ('!'): {
      // source func name
      mPrinters.push_back(std::make_unique<details::SourceFunctionPrinter<Alignment>>(padding));
      break;
    }

    case ('%'): {
      // % char
      mPrinters.push_back(std::make_unique<details::CharPrinter>('%'));
      break;
    }

    case ('u'): {
      // elapsed time since last log message in nanos
      mPrinters.push_back(std::make_unique<details::ElapsedTimePrinter<Alignment, std::chrono::nanoseconds>>(padding));
      break;
    }

    case ('i'): {
      // elapsed time since last log message in micros
      mPrinters.push_back(std::make_unique<details::ElapsedTimePrinter<Alignment, std::chrono::microseconds>>(padding));
      break;
    }

    case ('o'): {
      // elapsed time since last log message in millis
      mPrinters.push_back(std::make_unique<details::ElapsedTimePrinter<Alignment, std::chrono::milliseconds>>(padding));
      break;
    }

    case ('O'): {
      // elapsed time since last log message in seconds
      mPrinters.push_back(std::make_unique<details::ElapsedTimePrinter<Alignment, std::chrono::seconds>>(padding));
      break;
    }

#ifndef MINILOG_NO_TLS // mdc formatter requires TLS support
    case ('&'): {
      mPrinters.push_back(std::make_unique<details::MdcPrinter<Alignment>>(padding));
      break;
    }
#endif

    default: {
      // Unknown flag appears as is
      auto unknown_flag = std::make_unique<details::AggregatePrinter>();

      if (!padding.truncate) {
        unknown_flag->addChar('%');
        unknown_flag->addChar(flag);
        mPrinters.push_back((std::move(unknown_flag)));
      }
      // prev char was '!' and should have been treated as function name flag instead of truncating flag:
      // minilog::setPattern("[%10!] %v") => "[      main] some message"
      // minilog::setPattern("[%3!!] %v") => "[mai] some message"
      else {
        padding.truncate = false;
        mPrinters.push_back(std::make_unique<details::SourceFunctionPrinter<Alignment>>(padding));
        unknown_flag->addChar(flag);
        mPrinters.push_back((std::move(unknown_flag)));
      }

      break;
    }
  }
}

details::PaddingInfo
PatternFormatter::handlePaddingInfo(std::string::const_iterator& it, const std::string::const_iterator& end) {
  using details::PaddingInfo;
  using details::ScopedAlignment;
  constexpr size_t max_width = 64;
  if (it == end) {
    return PaddingInfo{};
  }

  PaddingInfo::Alignment alignment;
  switch (*it) {
    case '-':
      alignment = PaddingInfo::Alignment::kRight;
      ++it;
      break;
    case '=': {
      alignment = PaddingInfo::Alignment::kCenter;
      ++it;
      break;
    }
    default: {
      alignment = PaddingInfo::Alignment::kLeft;
      break;
    }
  }

  if (it == end || !std::isdigit(static_cast<unsigned char>(*it))) {
    return PaddingInfo{}; // no padding if no digit found here
  }

  auto width = static_cast<size_t>(*it) - '0';
  for (++it; it != end && std::isdigit(static_cast<unsigned char>(*it)); ++it) {
    const auto digit = static_cast<size_t>(*it) - '0';
    width = width * 10 + digit;
  }

  // search for the optional truncate marker '!'
  bool truncate;
  if (it != end && *it == '!') {
    truncate = true;
    ++it;
  } else {
    truncate = false;
  }
  return PaddingInfo{ std::min<size_t>(width, max_width), alignment, truncate };
}

void PatternFormatter::compilePattern(const std::string& pattern) {
  const auto end = pattern.end();
  std::unique_ptr<details::AggregatePrinter> user_chars;
  mPrinters.clear();
  for (auto it = pattern.begin(); it != pattern.end(); ++it) {
    if (*it == '%') {
      if (user_chars) {
        // append user chars found so far
        mPrinters.push_back(std::move(user_chars));
      }

      const auto padding = handlePaddingInfo(++it, end);

      if (it != end) {
        if (padding.enabled) {
          handleFlag<details::ScopedAlignment>(*it, padding);
        } else {
          handleFlag<details::NullAlignment>(*it, padding);
        }
      } else {
        break;
      }
    } else {
      // chars not following the % sign should be displayed as is
      if (!user_chars) {
        user_chars = std::make_unique<details::AggregatePrinter>();
      }
      // Every literal character is appended, not only the first one after a flag.
      user_chars->addChar(*it);
    }
  }

  if (user_chars) {
    // append raw chars found so far
    mPrinters.push_back(std::move(user_chars));
  }
}

} // namespace minilog
