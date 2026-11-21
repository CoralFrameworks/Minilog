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

// =============================================================
//  tests_MiniLogCore.cpp
//
//  Covers the parts of minilog that are not a sink:
//
//    - the pattern formatter: every common flag, padding, truncation, time zones,
//      custom printers, clone() and the source fields
//    - the logger: level filtering, flush levels, formatting, sink replacement,
//      copy/move, clone and a null formatter
//    - the backtrace ring buffer, including a queue that overflows
//    - the registry: registration, replacement, global settings and shutdown
//    - the log macros and the source location they capture
//    - MDC, Message/MessageBuffer and the file/path helpers of Utils
//
//  Every check prints to stdout. The suite writes its files below
//  "minilog_core_test_logs" so it can be run directly or through ctest.
// =============================================================

#include <minilog/PatternFormatter.h>
#include <minilog/log.h>
#include <minilog/sinks/DistSink.h>
#include <minilog/sinks/NullSink.h>
#include <minilog/sinks/OStreamSink.h>

#include "private/minilog/BackTracer.h"
#include "private/minilog/Message.h"
#include "private/minilog/MessageQueue.h"
#include "private/minilog/Utils.h"
#include "private/minilog/mdc.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

int gFailures = 0;
int gChecks = 0;

void check(const bool condition, const std::string& what) {
  ++gChecks;
  if (condition) {
    std::printf("  ok   %s\n", what.c_str());
  } else {
    ++gFailures;
    std::printf("  FAIL %s\n", what.c_str());
  }
}

void section(const char* name) {
  std::printf("--- %s\n", name);
}

const std::string& outputDirectory() {
  static const std::string directory = "minilog_core_test_logs";
  return directory;
}

std::string join(const std::string& directory, const std::string& name) {
  return minilog::Utils::joinPath(directory, name);
}

std::string readFile(const std::string& path) {
  std::FILE* file = minilog::Utils::openFile(path, "rb");
  if (file == nullptr) {
    return {};
  }

  std::string content;
  char buffer[512];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    content.append(buffer, read);
  }
  std::fclose(file);
  return content;
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

std::string copyView(const std::string_view view) {
  return view.empty() ? std::string() : std::string(view.data(), view.size());
}

constexpr const char* kTestFile = "C:/src/dir/test.cpp";
constexpr int kTestLine = 42;
constexpr const char* kTestFunction = "main";

minilog::Source testSource() {
  return minilog::Source(kTestFile, kTestLine, kTestFunction);
}

minilog::Message message(const std::string_view text, const minilog::Level level = minilog::Level::kInfo,
  const std::string_view name = "logger") {
  return minilog::Message(testSource(), name, level, text);
}

minilog::Message timedMessage(const minilog::Clock::time_point time, const std::string_view text,
  const minilog::Level level = minilog::Level::kInfo, const std::string_view name = "logger") {
  return minilog::Message(time, testSource(), name, level, text);
}

std::string formatWith(const std::string& pattern, const minilog::Message& msg,
  const minilog::TimeZone timeZone = minilog::TimeZone::kUTC) {
  minilog::PatternFormatter formatter(pattern, timeZone);
  std::string formatted;
  formatter.format(msg, formatted);
  return formatted;
}

/** A clock time point of 1970-01-01 14:07:09.123456700 UTC, for the time-flag checks. */
minilog::Clock::time_point fixedAfternoon() {
  const auto duration = std::chrono::seconds(14 * 3600 + 7 * 60 + 9) + std::chrono::milliseconds(123) +
                        std::chrono::microseconds(456) + std::chrono::nanoseconds(700);
  return minilog::Clock::time_point{} + std::chrono::duration_cast<minilog::Clock::duration>(duration);
}

/** A sink that records the messages it receives, for the logger and registry tests. */
class RecordingSink final : public minilog::Sink {
public:
  struct Entry {
    minilog::Level level{ minilog::Level::kOff };
    std::string name;
    std::string payload;
    std::string filename;
    std::string function;
    int line{ 0 };
  };

  void log(const minilog::Message& msg) override {
    entries.push_back({ msg.level,
      copyView(msg.name),
      copyView(msg.payload),
      msg.source.filename == nullptr ? std::string() : std::string(msg.source.filename),
      msg.source.function == nullptr ? std::string() : std::string(msg.source.function),
      msg.source.line });
  }

  void flush() override { ++flushCount; }

  void setPattern(const std::string& /*pattern*/) override { ++patternCount; }

  void setFormatter(std::unique_ptr<minilog::Formatter> /*formatter*/) override { ++formatterCount; }

  [[nodiscard]] bool saw(const std::string& text) const {
    for (const auto& entry: entries) {
      if (contains(entry.payload, text)) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] const Entry* last() const { return entries.empty() ? nullptr : &entries.back(); }

  std::vector<Entry> entries;
  int flushCount{ 0 };
  int patternCount{ 0 };
  int formatterCount{ 0 };
};

/** A formatter that prefixes the payload, used to check that DistSink hands a formatter down. */
class MarkerFormatter final : public minilog::Formatter {
public:
  explicit MarkerFormatter(std::string marker)
    : mMarker(std::move(marker)) {
  }

  void format(const minilog::Message& msg, std::string& dest) override {
    dest.append(mMarker);
    dest.append(msg.payload.data(), msg.payload.size());
    dest.push_back('\n');
  }

  [[nodiscard]] std::unique_ptr<minilog::Formatter> clone() const override {
    ++clones;
    return std::make_unique<MarkerFormatter>(mMarker);
  }

  mutable int clones{ 0 };

private:
  std::string mMarker;
};

/** A custom pattern flag printer: %u prints the payload in upper case. */
class UpperCasePrinter final : public minilog::CustomPrinter {
public:
  void format(const minilog::Message& msg, const std::tm& /*time*/, std::string& dest) override {
    for (const char character: msg.payload) {
      dest.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
    }
  }

  [[nodiscard]] std::unique_ptr<minilog::CustomPrinter> clone() const override {
    return std::make_unique<UpperCasePrinter>();
  }
};

std::string strftimeLocal(const std::time_t time, const char* format) {
  const auto tm = minilog::Utils::localtime(time);
  char buffer[128] = {};
  const auto written = std::strftime(buffer, sizeof(buffer), format, &tm);
  return std::string(buffer, written);
}

std::string formatUtcOffset(const int offsetMinutes) {
  const int absoluteMinutes = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
  char buffer[8] = {};
  std::snprintf(buffer, sizeof(buffer), "%c%02d:%02d", offsetMinutes < 0 ? '-' : '+', absoluteMinutes / 60,
    absoluteMinutes % 60);
  return buffer;
}

void testPatternBasics() {
  section("pattern formatter: literals, levels and source");

  check(formatWith("%v", message("hello")) == std::string("hello") + MINILOG_EOL,
    "the payload and one end of line are printed");
  check(formatWith("[%v]", message("hello")) == std::string("[hello]") + MINILOG_EOL,
    "literal text around the flag is kept");

  check(formatWith("%l|%L|%n|%v", message("text", minilog::Level::kWarn, "core")) ==
          std::string("warning|W|core|text") + MINILOG_EOL,
    "the long level, the short level, the logger name and the payload are printed");

  check(formatWith("%s", message("x")) == std::string("test.cpp") + MINILOG_EOL,
    "%s prints the short source file name");
  check(formatWith("%g", message("x")) == std::string(kTestFile) + MINILOG_EOL,
    "%g prints the full source file name");
  check(formatWith("%#", message("x")) == std::string(std::to_string(kTestLine)) + MINILOG_EOL,
    "%# prints the source line");
  check(formatWith("%!", message("x")) == std::string(kTestFunction) + MINILOG_EOL,
    "%! prints the source function");
  check(formatWith("%@", message("x")) == std::string(kTestFile) + ":" + std::to_string(kTestLine) + MINILOG_EOL,
    "%@ prints the source file and line");

  // A message that was not created by a log macro has no source at all.
  const minilog::Message withoutSource(minilog::Source(), "logger", minilog::Level::kInfo, "bare");
  check(formatWith("%s|%#|%!|%@", withoutSource) == std::string("|||") + MINILOG_EOL,
    "an empty source prints nothing for the source flags");

  check(formatWith("%%", message("x")) == std::string("%") + MINILOG_EOL, "%% prints a percent sign");
  check(formatWith("%q", message("x")) == std::string("%q") + MINILOG_EOL,
    "an unknown flag is printed as it was written");

  // Padding: the default aligns by appending spaces in front, '-' appends behind, '=' centers.
  check(formatWith("%10v", message("abc")) == std::string("       abc") + MINILOG_EOL,
    "a width pads in front of the value");
  check(formatWith("%-10v", message("abc")) == std::string("abc       ") + MINILOG_EOL,
    "the '-' alignment pads behind the value");
  check(formatWith("%=10v", message("abc")) == std::string("   abc    ") + MINILOG_EOL,
    "'=' centers the value");
  check(formatWith("%2v", message("abcdef")) == std::string("abcdef") + MINILOG_EOL,
    "a width smaller than the value changes nothing");
  check(formatWith("%3!v", message("abcdef")) == std::string("abc") + MINILOG_EOL,
    "the '!' marker truncates the value");
  check(formatWith("%2!v", message("abcdef")) == std::string("ab") + MINILOG_EOL,
    "a truncating width of 2 keeps two characters");

  // setPattern() replaces the compiled flags of a formatter.
  minilog::PatternFormatter formatter("%v");
  std::string formatted;
  formatter.format(message("first"), formatted);
  formatter.setPattern("%l:%v");
  formatted.clear();
  formatter.format(message("second", minilog::Level::kError), formatted);
  check(formatted == std::string("error:second") + MINILOG_EOL, "setPattern recompiles the pattern");

  // A formatter appends to the destination instead of replacing it.
  formatted = "prefix ";
  formatter.format(message("tail"), formatted);
  check(formatted == std::string("prefix info:tail") + MINILOG_EOL,
    "format() appends to a non-empty destination");

  // clone() reproduces the same output.
  const auto cloned = formatter.clone();
  std::string clonedOutput;
  cloned->format(message("clone"), clonedOutput);
  check(clonedOutput == std::string("info:clone") + MINILOG_EOL, "clone() formats like the original");
}

void testPatternTime() {
  section("pattern formatter: time");

  minilog::mdc::clear();

  const auto epoch = minilog::Clock::time_point{};
  const auto epochMessage = timedMessage(epoch, "date");

  check(formatWith("%Y-%m-%d", epochMessage) == std::string("1970-01-01") + MINILOG_EOL,
    "the UTC date is formatted");
  check(formatWith("%C|%D", epochMessage) == std::string("70|01/01/70") + MINILOG_EOL,
    "the two-digit year and the short date are formatted");
  check(formatWith("%c", epochMessage) == std::string("Thu Jan 1 00:00:00 1970") + MINILOG_EOL,
    "the C date and time representation is formatted");
  check(formatWith("%a|%A|%b|%B", epochMessage) == std::string("Thu|Thursday|Jan|January") + MINILOG_EOL,
    "the weekday and month names are formatted");

  const auto afternoon = fixedAfternoon();
  const auto afternoonMessage = timedMessage(afternoon, "time");
  check(formatWith("%H:%M:%S", afternoonMessage) == std::string("14:07:09") + MINILOG_EOL,
    "the 24-hour time is formatted");
  check(formatWith("%R|%T", afternoonMessage) == std::string("14:07|14:07:09") + MINILOG_EOL,
    "the short and the ISO time are formatted");
  check(formatWith("%I %p %r", afternoonMessage) == std::string("02 PM 02:07:09 PM") + MINILOG_EOL,
    "the 12-hour clock and the meridiem are formatted");
  // The message stores a Clock::time_point, so the fraction is limited by the
  // resolution of std::chrono::system_clock. On Apple that is microseconds, so a
  // 700 ns component cannot be represented; read the expected value back from the
  // time point that was actually constructed instead of assuming nanoseconds.
  const auto afternoonSeconds = std::chrono::duration_cast<std::chrono::seconds>(afternoon.time_since_epoch());
  const auto afternoonFraction = afternoon.time_since_epoch() - afternoonSeconds;
  char afternoonFractionText[64];
  std::snprintf(afternoonFractionText, sizeof(afternoonFractionText), "%03lld|%06lld|%09lld",
    static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(afternoonFraction).count()),
    static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(afternoonFraction).count()),
    static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(afternoonFraction).count()));
  check(formatWith("%e|%f|%F", afternoonMessage) == std::string(afternoonFractionText) + MINILOG_EOL,
    "milliseconds, microseconds and nanoseconds are formatted");
  check(formatWith("%E", afternoonMessage) == std::string("50829") + MINILOG_EOL,
    "the seconds since the epoch are formatted");
  check(formatWith("%z", afternoonMessage, minilog::TimeZone::kUTC) == std::string("+00:00") + MINILOG_EOL,
    "the UTC offset of the UTC formatter is +00:00");

  // Midnight and noon are both hour 12 on the 12-hour clock.
  check(formatWith("%I %p", timedMessage(epoch, "midnight")) == std::string("12 AM") + MINILOG_EOL,
    "midnight is 12 AM");
  check(formatWith("%I %p", timedMessage(minilog::Clock::time_point{} + std::chrono::hours(12), "noon")) ==
          std::string("12 PM") + MINILOG_EOL,
    "noon is 12 PM");
  check(formatWith("%I:%M %p", timedMessage(minilog::Clock::time_point{} + std::chrono::minutes(12 * 60 + 30), "x")) ==
          std::string("12:30 PM") + MINILOG_EOL,
    "half past noon is 12:30 PM");

  // The local offset is whatever the machine is set to; compare against the same helper the formatter uses.
  const auto now = minilog::Clock::now();
  const auto localTm = minilog::Utils::localtime(minilog::Clock::to_time_t(now));
  const int offsetMinutes = minilog::Utils::utcMinutesOffset(localTm);
  check(formatWith("%z", timedMessage(now, "local"), minilog::TimeZone::kLocal) ==
          formatUtcOffset(offsetMinutes) + MINILOG_EOL,
    "the local UTC offset is formatted");

  // The very first message can be at the epoch; the offset cache must still be filled on that first call.
  const int epochOffsetMinutes = minilog::Utils::utcMinutesOffset(minilog::Utils::localtime(0));
  check(formatWith("%z", epochMessage, minilog::TimeZone::kLocal) == formatUtcOffset(epochOffsetMinutes) + MINILOG_EOL,
    "the local UTC offset of a message at the epoch is formatted");

  // %+ is the default pattern of a sink and the formatter built without one.
  check(formatWith("%+", afternoonMessage) ==
          std::string("[1970-01-01 14:07:09.123] [logger] [info] [test.cpp:42] time") + MINILOG_EOL,
    "%+ prints time, logger, level, source and payload");
  minilog::PatternFormatter defaultFormatter(minilog::TimeZone::kUTC);
  std::string defaultOutput;
  defaultFormatter.format(afternoonMessage, defaultOutput);
  check(defaultOutput == std::string("[1970-01-01 14:07:09.123] [logger] [info] [test.cpp:42] time") + MINILOG_EOL,
    "the time-zone-only constructor defaults to %+");

  // A fresh formatter must fill its time cache even for a message at the epoch, whose second is zero.
  minilog::PatternFormatter epochFormatter(minilog::TimeZone::kUTC);
  std::string epochOutput;
  epochFormatter.format(epochMessage, epochOutput);
  check(epochOutput == std::string("[1970-01-01 00:00:00.000] [logger] [info] [test.cpp:42] date") + MINILOG_EOL,
    "the default formatter formats a message at the epoch");

  // The cached date of %+ is refreshed when the second changes, and the milliseconds are not cached.
  minilog::PatternFormatter caching("%H:%M:%S.%e", minilog::TimeZone::kUTC);
  std::string first;
  std::string second;
  caching.format(timedMessage(afternoon, "first"), first);
  caching.format(timedMessage(afternoon + std::chrono::milliseconds(500), "second"), second);
  check(first == std::string("14:07:09.123") + MINILOG_EOL &&
          second == std::string("14:07:09.623") + MINILOG_EOL,
    "two messages of the same second but different milliseconds are both formatted");

  // The MDC is part of %+ when it is not empty. A build with MINILOG_NO_TLS has no MDC printer at all.
#ifndef MINILOG_NO_TLS
  minilog::mdc::put("request", "abc");
  minilog::mdc::put("user", "u1");
  check(formatWith("%+", afternoonMessage) ==
          std::string("[1970-01-01 14:07:09.123] [logger] [info] [test.cpp:42] [request:abc user:u1] time") +
            MINILOG_EOL,
    "%+ includes the MDC");
  minilog::mdc::clear();
#else
  check(true, "%+ does not include the MDC when TLS is disabled");
#endif
}

void testPatternCustomPrinter() {
  section("pattern formatter: custom printers");

  minilog::PatternFormatter::CustomPrinters printers;
  printers['u'] = std::make_shared<UpperCasePrinter>();

  minilog::PatternFormatter formatter("%u %v", minilog::TimeZone::kUTC, minilog::Utils::defaultEol(),
    std::move(printers));
  std::string formatted;
  formatter.format(message("hello", minilog::Level::kWarn), formatted);
  check(formatted == std::string("HELLO hello") + MINILOG_EOL, "a custom flag uses the registered printer");

  const auto cloned = formatter.clone();
  std::string clonedOutput;
  cloned->format(message("clone"), clonedOutput);
  check(clonedOutput == std::string("CLONE clone") + MINILOG_EOL, "a custom printer is cloned with the formatter");

  minilog::PatternFormatter fallback("%Z");
  std::string fallbackOutput;
  fallback.format(message("x"), fallbackOutput);
  check(fallbackOutput == std::string("%Z") + MINILOG_EOL,
    "an unknown flag is printed as it was written");
}

void testLogger() {
  section("logger");

  std::ostringstream stream;
  auto sink = std::make_shared<minilog::OStreamSinkMt>(stream);
  sink->setPattern("%l|%n|%v");

  minilog::Logger logger("core", sink);
  check(logger.name() == "core", "the logger keeps its name");
  check(logger.level() == minilog::Level::kTrace, "a logger starts at trace");
  check(logger.sinks().size() == 1, "the logger holds its sink");
  check(logger.shouldLog(minilog::Level::kTrace), "trace is enabled at the trace level");

  logger.setLevel(minilog::Level::kInfo);
  check(logger.level() == minilog::Level::kInfo, "setLevel changes the level");
  check(!logger.shouldLog(minilog::Level::kDebug), "debug is filtered at the info level");
  check(logger.shouldLog(minilog::Level::kCritical), "critical is enabled at the info level");

  logger.debug("hidden");
  logger.info("shown {}", 7);
  logger.log(minilog::Level::kWarn, "direct {}", 8);
  logger.log(minilog::Source("other.cpp", 3, "fn"), minilog::Level::kError, "source {}", 9);
  logger.info(std::string("runtime string"));

  const auto output = stream.str();
  check(!contains(output, "hidden"), "a message below the level is not formatted");
  check(contains(output, "info|core|shown 7"), "info() formats its arguments");
  check(contains(output, "warning|core|direct 8"), "log(Level, ...) formats its arguments");
  check(contains(output, "error|core|source 9"), "log(Source, Level, ...) uses the logger name");
  check(contains(output, "info|core|runtime string"), "a runtime string is logged without formatting");

  // A sink has a level of its own, which filters once more.
  std::ostringstream sinkStream;
  auto sinkSink = std::make_shared<minilog::OStreamSinkMt>(sinkStream);
  sinkSink->setPattern("%v");
  sinkSink->setLevel(minilog::Level::kError);
  minilog::Logger filtered("filtered", sinkSink);
  filtered.info("not written");
  filtered.error("written");
  check(!contains(sinkStream.str(), "not written"), "the level of a sink filters the message");
  check(contains(sinkStream.str(), "written"), "the level of a sink admits the message");
  check(sinkSink->getLevel() == minilog::Level::kError, "the level of a sink is readable");

  // The flush level decides when the logger flushes its sinks after a message.
  auto recording = std::make_shared<RecordingSink>();
  minilog::Logger flushing("flushing", recording);
  flushing.setLevel(minilog::Level::kTrace);
  flushing.flushLevel(minilog::Level::kError);
  flushing.info("no flush");
  check(recording->flushCount == 0, "a message below the flush level does not flush");
  flushing.error("flush");
  check(recording->flushCount == 1, "a message at the flush level flushes every sink");
  flushing.critical("flush again");
  check(recording->flushCount == 2, "a message above the flush level flushes too");
  flushing.flush();
  check(recording->flushCount == 3, "flush() flushes on demand");

  // setSinks flushes the old sinks before they are released.
  auto oldSink = std::make_shared<RecordingSink>();
  auto newSink = std::make_shared<RecordingSink>();
  minilog::Logger replaced("replaced", oldSink);
  replaced.setSinks({ newSink });
  check(oldSink->flushCount == 1, "setSinks flushes the sinks that are replaced");
  check(replaced.sinks().size() == 1 && replaced.sinks().front() == newSink, "setSinks installs the new sinks");

  // An empty formatter is refused instead of crashing half way through the sink list.
  std::ostringstream nullStream;
  auto nullSink = std::make_shared<minilog::OStreamSinkMt>(nullStream);
  nullSink->setPattern("%v");
  minilog::Logger nullFormatter("null-formatter", { sink, nullSink });
  nullFormatter.setFormatter(nullptr);
  nullFormatter.info("still formats");
  check(contains(nullStream.str(), "still formats"), "setFormatter(nullptr) is refused and logging keeps working");

  // setPattern reaches every sink.
  std::ostringstream patternedStream;
  auto patterned = std::make_shared<minilog::OStreamSinkMt>(patternedStream);
  minilog::Logger patternedLogger("patterned", patterned);
  patternedLogger.setPattern("%L:%v");
  patternedLogger.info("pattern");
  check(patternedStream.str() == std::string("I:pattern") + MINILOG_EOL, "setPattern applies to the sinks");

  // Copy, move and clone.
  auto cloned = logger.clone("clone");
  check(cloned->name() == "clone", "clone() takes the new name");
  check(cloned->level() == logger.level(), "clone() keeps the level");
  check(cloned->sinks() == logger.sinks(), "clone() shares the sinks");

  minilog::Logger copied(logger);
  check(copied.name() == logger.name() && copied.sinks() == logger.sinks(),
    "the copy constructor copies name and sinks");

  minilog::Logger moved(std::move(copied));
  check(moved.name() == "core" && moved.sinks() == logger.sinks(), "the move constructor moves name and sinks");
}

void testBacktrace() {
  section("backtrace");

  std::ostringstream stream;
  auto sink = std::make_shared<minilog::OStreamSinkMt>(stream);
  sink->setPattern("%v");

  minilog::Logger logger("backtrace", sink);
  logger.setLevel(minilog::Level::kError);
  logger.enableBacktrace(3);
  check(logger.shouldTrace(), "enableBacktrace enables the trace");

  for (int i = 0; i < 5; ++i) {
    logger.info("trace " + std::to_string(i));
  }
  logger.error("the error");

  // The queue keeps the newest three of the six messages: trace 3, trace 4 and the error.
  logger.dumpBacktrace();
  check(contains(stream.str(), "Backtrace Start"), "dumpBacktrace opens the backtrace");
  check(contains(stream.str(), "Backtrace End"), "dumpBacktrace closes the backtrace");
  check(contains(stream.str(), "trace 3") && contains(stream.str(), "trace 4"),
    "the newest captured messages are dumped");
  check(contains(stream.str(), "the error"), "the message that triggered the dump is included");
  check(!contains(stream.str(), "trace 0") && !contains(stream.str(), "trace 1"),
    "the oldest messages are dropped by the ring buffer");
  const auto dumped = stream.str();
  check(std::count(dumped.begin(), dumped.end(), '\n') == 6,
    "the stream contains the live error, two markers and three captured messages");

  // The dump empties the queue; a second dump has nothing to show.
  const auto afterFirstDump = stream.str();
  logger.dumpBacktrace();
  check(stream.str() == afterFirstDump, "the backtrace is emptied by the dump");

  logger.disableTrace();
  check(!logger.shouldTrace(), "disableTrace disables the trace");
  logger.info("not captured");
  logger.error("another error");
  const auto afterDisable = stream.str();
  logger.dumpBacktrace();
  check(stream.str() == afterDisable, "a disabled trace captures nothing");

  // Zero is a valid request that must not produce an enabled queue without slots.
  logger.enableBacktrace(0);
  check(!logger.shouldTrace(), "a backtrace of zero messages is disabled");
  logger.info("nothing is captured");
  logger.dumpBacktrace();
  check(stream.str() == afterDisable, "a disabled zero-sized backtrace dumps nothing");

  // The ring buffer itself is tested directly as well; this is the regression test for its off-by-one.
  minilog::MessageQueue<int> queue(3);
  for (int i = 0; i < 5; ++i) {
    queue.push_back(int(i));
  }
  check(queue.size() == 3, "the message queue keeps its capacity");
  check(queue.front() == 2 && queue.at(1) == 3 && queue.at(2) == 4,
    "the message queue drops the oldest items and keeps the order");
  check(queue.full(), "a full message queue reports full");
  queue.pop_front();
  check(queue.size() == 2 && queue.front() == 3, "pop_front drops one item");
  check(!queue.empty(), "a non-empty message queue is not empty");

  minilog::MessageQueue<int> emptyQueue(0);
  emptyQueue.push_back(1);
  check(emptyQueue.size() == 0 && emptyQueue.empty(), "a zero-capacity message queue drops every item");
  minilog::MessageQueue<int> defaultQueue;
  defaultQueue.push_back(1);
  check(defaultQueue.empty(), "a default-constructed message queue has no capacity");

  minilog::MessageQueue<int> copiedQueue(queue);
  check(copiedQueue.size() == queue.size() && copiedQueue.front() == queue.front(),
    "the message queue is copyable");
  minilog::MessageQueue<int> movedQueue(std::move(copiedQueue));
  check(movedQueue.size() == queue.size() && movedQueue.front() == queue.front() &&
          copiedQueue.empty(),
    "the message queue is movable and leaves the source empty");
}

void testMdc() {
  section("mdc");

  minilog::mdc::clear();
  check(minilog::mdc::getContext().empty(), "the MDC starts empty");
  check(minilog::mdc::get("missing").empty(), "a missing key returns an empty string");

  minilog::mdc::put("request", "42");
  minilog::mdc::put("user", "alice");
  check(minilog::mdc::get("request") == "42", "put and get address the same key");
  check(minilog::mdc::getContext().size() == 2, "both keys are in the context");

#ifndef MINILOG_NO_TLS
  check(formatWith("%&", message("text")) == std::string("request:42 user:alice") + MINILOG_EOL,
    "%& prints the context as key:value pairs");
  check(formatWith("%+", timedMessage(minilog::Clock::time_point{}, "text")) ==
          std::string("[1970-01-01 00:00:00.000] [logger] [info] [test.cpp:42] [request:42 user:alice] text") +
            MINILOG_EOL,
    "%+ includes the context");
#else
  check(formatWith("%&", message("text")) == std::string("%&") + MINILOG_EOL,
    "%& is an unknown flag when TLS is disabled");
#endif

  minilog::mdc::remove("request");
  check(minilog::mdc::get("request").empty(), "remove deletes one key");
  check(minilog::mdc::getContext().size() == 1, "the other key survives remove");

  minilog::mdc::clear();
  check(minilog::mdc::getContext().empty(), "clear deletes the whole context");
#ifndef MINILOG_NO_TLS
  check(formatWith("%&", message("text")) == std::string("") + MINILOG_EOL,
    "an empty context prints nothing");
#else
  check(true, "an empty context has no printer when TLS is disabled");
#endif

  // The context is thread local, so another thread starts with its own empty map.
  minilog::mdc::put("owner", "main");
  std::string otherThreadValue = "not run";
  std::thread worker([&otherThreadValue]() {
    otherThreadValue = minilog::mdc::get("owner");
    minilog::mdc::put("owner", "worker");
  });
  worker.join();
  check(otherThreadValue.empty(), "another thread does not see the context of this one");
  check(minilog::mdc::get("owner") == "main", "the other thread does not modify this thread's context");
  minilog::mdc::clear();
}

void testMessageAndBuffer() {
  section("message and message buffer");

  check(minilog::Source().empty(), "a default source is empty");
  check(minilog::Source("x.cpp", 0, "fn").empty(), "a source without a line is empty");
  check(!testSource().empty(), "a source with a line is not empty");

  const auto now = minilog::Clock::now();
  const minilog::Message msg(now, testSource(), "logger", minilog::Level::kWarn, "payload");
  check(msg.name == "logger", "the message keeps its logger name");
  check(msg.level == minilog::Level::kWarn, "the message keeps its level");
  check(msg.time == now, "the message keeps its time");
  check(msg.source.line == kTestLine && msg.source.function == std::string_view(kTestFunction),
    "the message keeps its source");
  check(msg.payload == "payload", "the message keeps its payload");
#if defined(MINILOG_NO_THREAD_ID)
  check(msg.thread_id == 0, "the message leaves the thread id at zero when it is disabled");
#else
  check(msg.thread_id != 0, "the message records the thread id");
#endif

  // The string views of a MessageBuffer must point into the buffer that the object owns, not into the
  // temporary strings the source Message was built from.
  const auto makeBuffered = []() {
    std::string name = "buffered";
    std::string text = "payload text";
    return minilog::MessageBuffer(
      minilog::Message(testSource(), name, minilog::Level::kError, text));
  };

  auto buffer = makeBuffered();
  check(buffer.name == "buffered", "the buffer owns a copy of the name");
  check(buffer.payload == "payload text", "the buffer owns a copy of the payload");
  check(buffer.level == minilog::Level::kError, "the buffer keeps the level");

  minilog::MessageBuffer copy(buffer);
  check(copy.name == "buffered" && copy.payload == "payload text", "the buffer copy keeps both strings");
  check(copy.name.data() != buffer.name.data(), "the buffer copy owns its own storage");

  minilog::MessageBuffer assigned = buffer;
  assigned = buffer;
  check(assigned.name == "buffered" && assigned.payload == "payload text",
    "self-assignment of a message buffer keeps its strings");

  minilog::MessageBuffer moved(std::move(assigned));
  check(moved.name == "buffered" && moved.payload == "payload text", "the moved buffer keeps its strings");

  const minilog::Message emptyName(minilog::Source(), std::string_view(), minilog::Level::kInfo, "only payload");
  const minilog::MessageBuffer emptyNameBuffer(emptyName);
  check(emptyNameBuffer.name.empty() && emptyNameBuffer.payload == "only payload",
    "an empty name is buffered correctly");
}

void testUtils() {
  section("utils");

  check(minilog::Utils::dirname("logs/coral.log") == "logs/", "dirname keeps the separator");
  check(minilog::Utils::dirname("coral.log").empty(), "dirname of a bare name is empty");
  check(minilog::Utils::dirname("C:\\logs\\coral.log") == "C:\\logs\\", "dirname accepts backslashes");
  check(minilog::Utils::basename("logs/coral.log") == "coral.log", "basename drops the directory");
  check(minilog::Utils::basename("C:\\logs\\coral.log") == "coral.log", "basename accepts backslashes");
  check(minilog::Utils::stem("logs/coral.log") == "coral", "stem drops the extension");
  check(minilog::Utils::stem("logs/coral") == "coral", "stem without an extension is the name");
  check(minilog::Utils::stem("logs/.hidden") == ".hidden", "a leading dot is not an extension");
  check(minilog::Utils::extension("logs/coral.log") == ".log", "extension keeps the dot");
  check(minilog::Utils::extension("logs/coral") == "", "a name without an extension has none");
  check(minilog::Utils::extension("logs/.hidden") == "", "a leading dot is not an extension for extension()");

  check(minilog::Utils::joinPath("logs", "coral.log") == "logs/coral.log", "joinPath adds a separator");
  check(minilog::Utils::joinPath("logs/", "coral.log") == "logs/coral.log",
    "joinPath does not double a separator");
  check(minilog::Utils::joinPath("C:\\logs", "coral.log") == "C:\\logs/coral.log",
    "joinPath accepts a backslash directory");
  check(minilog::Utils::joinPath("", "coral.log") == "coral.log", "an empty directory is ignored");

  check(minilog::Utils::formatTime(0, "%Y-%m-%d") == strftimeLocal(0, "%Y-%m-%d"),
    "formatTime uses the local time");
  check(minilog::Utils::formatTime(0, "") == strftimeLocal(0, "%Y-%m-%d"),
    "an empty format defaults to the date");
  check(minilog::Utils::formatTime(0, nullptr) == strftimeLocal(0, "%Y-%m-%d"),
    "a null format defaults to the date");

  check(minilog::Utils::threadId() != 0, "the thread id is not zero");
  check(minilog::Utils::pid() > 0, "the process id is positive");
  check(minilog::Utils::now() != minilog::Clock::time_point{}, "now() returns a real time point");
  const auto before = minilog::Clock::now();
  minilog::Utils::usleep(1);
  check(minilog::Clock::now() - before >= std::chrono::milliseconds(1), "usleep waits");
  check(std::abs(minilog::Utils::utcMinutesOffset()) <= 14 * 60,
    "the UTC offset is within the range of a time zone");

  const auto directory = join(outputDirectory(), "utils");
  check(minilog::Utils::createDirectories(directory), "createDirectories creates a nested directory");
  check(std::filesystem::is_directory(directory), "the directory exists after createDirectories");
  check(minilog::Utils::createDirectories(directory), "createDirectories accepts an existing directory");
  check(minilog::Utils::createDirectories(""), "an empty directory is accepted");

  const auto filePath = join(directory, "file.txt");
  std::FILE* file = minilog::Utils::openFile(filePath, "wb");
  check(file != nullptr, "openFile opens a file for writing");
  if (file != nullptr) {
    check(std::fwrite("abc", 1, 3, file) == 3, "the file accepts a write");
    std::fclose(file);
  }
  check(minilog::Utils::fileExists(filePath), "fileExists sees the written file");
  check(readFile(filePath) == "abc", "the written bytes are readable");
  check(minilog::Utils::openFile(join(directory, "missing/file.txt"), "wb") == nullptr,
    "openFile returns null when the parent directory does not exist");

  const auto source = join(directory, "source.txt");
  const auto target = join(directory, "target.txt");
  std::FILE* sourceFile = minilog::Utils::openFile(source, "wb");
  if (sourceFile != nullptr) {
    std::fwrite("new", 1, 3, sourceFile);
    std::fclose(sourceFile);
  }
  std::FILE* targetFile = minilog::Utils::openFile(target, "wb");
  if (targetFile != nullptr) {
    std::fwrite("old", 1, 3, targetFile);
    std::fclose(targetFile);
  }
  std::string error;
  check(minilog::Utils::renameFile(source, target, &error), "renameFile replaces an existing target");
  check(readFile(target) == "new", "the renamed file has the source content");
  check(!minilog::Utils::fileExists(source), "the source is gone after the rename");

  check(minilog::Utils::removeFile(target, &error), "removeFile removes an existing file");
  check(!minilog::Utils::fileExists(target), "the removed file is gone");

  error.clear();
  check(!minilog::Utils::renameFile(join(directory, "nothing.txt"), join(directory, "no-target.txt"), &error),
    "renameFile fails on a missing source");
  check(!error.empty(), "a failed rename reports an error");

  // A failed rename must not remove the existing target before it discovers that the source is missing.
  const auto kept = join(directory, "kept.txt");
  std::FILE* keptFile = minilog::Utils::openFile(kept, "wb");
  if (keptFile != nullptr) {
    std::fwrite("keep", 1, 4, keptFile);
    std::fclose(keptFile);
  }
  error.clear();
  check(!minilog::Utils::renameFile(join(directory, "nothing.txt"), kept, &error),
    "renameFile fails on a missing source even when the target exists");
  check(readFile(kept) == "keep", "a failed rename keeps the previous target intact");
  check(minilog::Utils::removeFile(kept), "cleanup: the target of the failed rename");

  error.clear();
  check(!minilog::Utils::removeFile(target, &error), "removeFile fails on a missing file");
  check(!error.empty(), "a failed remove reports an error");

  const minilog::MiniLogException plain("a plain error");
  check(std::string(plain.what()) == "a plain error", "MiniLogException keeps its message");
  const minilog::MiniLogException system("a system error", ENOENT);
  check(contains(system.what(), "a system error") && std::strlen(system.what()) > std::strlen("a system error"),
    "MiniLogException appends the system error");
}

void testRegistry() {
  section("registry");

  minilog::shutdown();

  auto defaultSink = std::make_shared<RecordingSink>();
  auto defaultLogger = std::make_shared<minilog::Logger>("registry-default", defaultSink);
  defaultLogger->setPattern("%v");
  minilog::setDefaultLogger(defaultLogger);
  check(minilog::defaultLogger() == defaultLogger, "setDefaultLogger installs the default logger");
  check(minilog::getLogger("registry-default") == defaultLogger, "the default logger is registered by name");

  check(minilog::getLogger("missing") == nullptr, "getLogger returns null for an unknown name");

  auto alpha = std::make_shared<minilog::Logger>("alpha", std::make_shared<minilog::NullSinkMt>());
  minilog::registerLogger(alpha);
  check(minilog::getLogger("alpha") == alpha, "registerLogger registers the logger");

  bool threw = false;
  try {
    minilog::registerLogger(alpha);
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "registering the same name twice is refused");

  auto beta = std::make_shared<minilog::Logger>("alpha", std::make_shared<minilog::NullSinkMt>());
  minilog::registerOrReplace(beta);
  check(minilog::getLogger("alpha") == beta, "registerOrReplace replaces a logger with the same name");

  // initializeLogger applies the global formatter, level, flush level and backtrace.
  minilog::setPattern("%l|%v");
  minilog::setLevel(minilog::Level::kWarn);
  minilog::flushLevel(minilog::Level::kError);
  minilog::enableBacktrace(2);

  std::ostringstream formattedStream;
  auto formattedSink = std::make_shared<minilog::OStreamSinkMt>(formattedStream);
  auto initialized = std::make_shared<minilog::Logger>("initialized", formattedSink);
  minilog::initializeLogger(initialized);
  check(minilog::getLogger("initialized") == initialized, "initializeLogger registers the logger");
  check(initialized->level() == minilog::Level::kWarn, "initializeLogger applies the global level");
  check(initialized->flushLevel() == minilog::Level::kError,
    "initializeLogger applies the global flush level rather than the log level");
  check(initialized->shouldTrace(), "initializeLogger applies the global backtrace");
  initialized->warn("formatted");
  check(formattedStream.str() == std::string("warning|formatted") + MINILOG_EOL,
    "initializeLogger applies the global pattern");
  check(initialized->sinks().front()->getLevel() == minilog::Level::kTrace,
    "initializeLogger leaves the level of a sink alone");

  // A null global formatter is refused and does not disturb the current formatting.
  std::ostringstream nullStream;
  auto nullSink = std::make_shared<minilog::OStreamSinkMt>(nullStream);
  auto nullLogger = std::make_shared<minilog::Logger>("null-formatter", nullSink);
  nullLogger->setPattern("%v");
  minilog::registerOrReplace(nullLogger);
  minilog::setFormatter(nullptr);
  nullLogger->info("after null");
  check(nullStream.str() == std::string("after null") + MINILOG_EOL,
    "a null global formatter is refused and logging keeps working");

  // Replacing the logger that is currently the default moves the default pointer too.
  auto holder = std::make_shared<minilog::Logger>("holder", std::make_shared<minilog::NullSinkMt>());
  minilog::setDefaultLogger(holder);
  auto replacement = std::make_shared<minilog::Logger>("holder", std::make_shared<minilog::NullSinkMt>());
  minilog::registerOrReplace(replacement);
  check(minilog::defaultLogger() == replacement,
    "registerOrReplace moves the default pointer when the default name is replaced");
  check(minilog::getLogger("holder") == replacement, "getLogger returns the replacement as well");

  // dropAll is the registry-level cleanup.
  check(minilog::getLogger("alpha") == beta, "the replaced logger is still reachable");
  minilog::dropAll();
  check(minilog::getLogger("alpha") == nullptr, "dropAll removes the named loggers");
  check(minilog::defaultLogger() != nullptr, "the default logger is re-created after dropAll");
  check(minilog::defaultLogger()->name().empty(), "the re-created default logger is the platform default");

  minilog::setDefaultLogger(nullptr);
  check(minilog::defaultLogger() != nullptr, "the default logger is re-created after it is forgotten");

  minilog::setLevel(minilog::Level::kDebug);
  auto later = std::make_shared<minilog::Logger>("later", std::make_shared<minilog::NullSinkMt>());
  minilog::initializeLogger(later);
  check(later->level() == minilog::Level::kDebug, "a logger created later starts at the current global level");

  minilog::flushAll();
  check(true, "flushAll does not fail");

  minilog::shutdown();
  check(minilog::defaultLogger() != nullptr, "logging is possible after shutdown");
  check(minilog::getLogger("later") == nullptr, "shutdown drops the registered loggers");

  // Restore the defaults for the rest of the process.
  minilog::setLevel(minilog::Level::kTrace);
  minilog::flushLevel(minilog::Level::kOff);
  minilog::disableBacktrace();
  minilog::setPattern("%+");
}

void testMacros() {
  section("log macros");

  auto sink = std::make_shared<RecordingSink>();
  auto defaultLogger = std::make_shared<minilog::Logger>("macros", sink);
  defaultLogger->setLevel(minilog::Level::kTrace);
  minilog::setDefaultLogger(defaultLogger);

  MINILOG_INFO("macro {}", 7);
  const auto* entry = sink->last();
  check(entry != nullptr && entry->payload == "macro 7", "MINILOG_INFO formats its arguments");
  check(entry != nullptr && entry->name == "macros", "the macro uses the default logger");
#ifndef MINILOG_NO_SOURCE
  check(entry != nullptr && entry->line > 0, "the macro records the source line");
  check(entry != nullptr && contains(entry->filename, "tests_MiniLogCore.cpp"),
    "the macro records the source file");
  check(entry != nullptr && !entry->function.empty(), "the macro records the source function");
#else
  check(entry != nullptr && entry->line == 0 && entry->filename.empty() && entry->function.empty(),
    "MINILOG_NO_SOURCE leaves the source empty");
#endif

  MINILOG_LOGGER_WARN(defaultLogger, "direct {}", 3);
  entry = sink->last();
  check(entry != nullptr && entry->level == minilog::Level::kWarn && entry->payload == "direct 3",
    "MINILOG_LOGGER_WARN uses the given logger");

#if MINILOG_ACTIVE_LEVEL <= MINILOG_LEVEL_TRACE
  MINILOG_TRACE("trace {}", 1);
  check(sink->last() != nullptr && sink->last()->payload == "trace 1", "MINILOG_TRACE is compiled in");
#else
  check(true, "MINILOG_TRACE is compiled out at this active level");
#endif

#if MINILOG_ACTIVE_LEVEL <= MINILOG_LEVEL_DEBUG
  MINILOG_DEBUG("debug {}", 2);
  check(sink->last() != nullptr && sink->last()->payload == "debug 2", "MINILOG_DEBUG is compiled in");
#else
  check(true, "MINILOG_DEBUG is compiled out at this active level");
#endif

  MINILOG_ERROR("error {}", 3);
  check(sink->last() != nullptr && sink->last()->level == minilog::Level::kError, "MINILOG_ERROR works");
  MINILOG_CRITICAL("critical {}", 4);
  check(sink->last() != nullptr && sink->last()->level == minilog::Level::kCritical, "MINILOG_CRITICAL works");

  minilog::shutdown();
}

void testDistFormatter() {
  section("dist sink formatter");

  std::ostringstream first;
  std::ostringstream second;
  auto firstSink = std::make_shared<minilog::OStreamSinkMt>(first);
  auto secondSink = std::make_shared<minilog::OStreamSinkMt>(second);

  auto dist = std::make_shared<minilog::DistSinkMt>();
  dist->addSink(firstSink);
  dist->addSink(secondSink);

  auto marker = std::make_unique<MarkerFormatter>("M|");
  auto* rawMarker = marker.get();
  dist->setFormatter(std::move(marker));
  check(rawMarker->clones == 1, "DistSink clones the formatter for every sink but the last one");

  dist->log(message("hello"));
  check(first.str() == "M|hello\n", "the first contained sink got a cloned formatter");
  check(second.str() == "M|hello\n", "the last contained sink got the original formatter");

  // The pattern and the level are handed down as well.
  std::ostringstream patterned;
  auto patternedSink = std::make_shared<minilog::OStreamSinkMt>(patterned);
  auto patternedDist = std::make_shared<minilog::DistSinkMt>();
  patternedDist->addSink(patternedSink);
  patternedDist->setPattern("%l:%v");
  patternedDist->setLevel(minilog::Level::kError);
  patternedDist->log(message("visible", minilog::Level::kWarn));
  check(patterned.str().empty(), "the level handed down filters a message below it");
  patternedDist->log(message("visible", minilog::Level::kError));
  check(patterned.str() == std::string("error:visible") + MINILOG_EOL,
    "the pattern handed down formats the message");
  check(patternedDist->size() == 1 && patternedDist->sinks().front() == patternedSink,
    "the contained sink is reachable");
  patternedDist->removeSink(firstSink);
  check(patternedDist->size() == 1, "removing a sink that is not there is a no-op");
}

} // namespace anonymous

int main() {
  std::error_code ec;
  std::filesystem::remove_all(outputDirectory(), ec);
  if (!minilog::Utils::createDirectories(outputDirectory())) {
    std::printf("cannot create the output directory '%s'\n", outputDirectory().c_str());
    return 1;
  }

  std::printf("minilog core tests, output directory: %s\n",
    std::filesystem::absolute(outputDirectory()).string().c_str());

  testPatternBasics();
  testPatternTime();
  testPatternCustomPrinter();
  testLogger();
  testBacktrace();
  testMdc();
  testMessageAndBuffer();
  testUtils();
  testDistFormatter();
  testRegistry();
  testMacros();

  std::printf("%d checks, %d failures\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
