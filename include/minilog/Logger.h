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

#ifndef MINILOG_LOGGER_H
#define MINILOG_LOGGER_H

#include "private/minilog/BackTracer.h"

#include <minilog/Sink.h>
#include <minilog/common.h>

#include <chrono>
#include <format>
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace minilog {

class Logger {
public:
  virtual ~Logger() = default;

  // Empty Logger
  explicit Logger(std::string name)
    : mName(std::move(name)),
      mSinks() {
  }

  // Logger with range on sinks
  template<typename iter>
  Logger(std::string name, iter begin, iter end)
    : mName(std::move(name)),
      mSinks(begin, end) {
  }

  // Logger with single sink
  Logger(std::string name, std::shared_ptr<Sink> sink)
    : mName(std::move(name)),
      mSinks({ std::move(sink) }) {
  }

  // Logger with sinks init list
  Logger(std::string name, const std::initializer_list<std::shared_ptr<Sink>> sinks)
    : Logger(std::move(name), sinks.begin(), sinks.end()) {
  }

  Logger(const Logger& other);
  Logger(Logger&& other) noexcept;
  Logger& operator=(Logger other) noexcept;

  void swap(Logger& other) noexcept;

  template<typename... Args>
  void log(Source source, Level level, format_string_t<Args...> fmt, Args&&... args) {
#if defined(__cpp_lib_format) && __cpp_lib_format >= 202207L
    onLog(source, level, fmt.get(), std::forward<Args>(args)...);
#else
    onLog(source, level, fmt, std::forward<Args>(args)...);
#endif
  }

  template<typename... Args>
  void log(Level level, format_string_t<Args...> fmt, Args&&... args) {
    log(Source(), level, fmt, std::forward<Args>(args)...);
  }

  template<typename T>
  void log(Level level, const T& msg) {
    log(Source(), level, msg);
  }

  template<class T, typename std::enable_if<!is_convertible_to_basic_format_string<const T&>::value, int>::type = 0>
  void log(Source source, Level level, const T& msg) {
    log(source, level, "{}", msg);
  }

  void log(const Clock::time_point time, const Source& source, const Level level, const std::string_view msg) {
    const bool logEnabled = shouldLog(level);
    const bool trackbackEnabled = mTracer.enabled();
    if (!logEnabled && !trackbackEnabled) {
      return;
    }

    const auto logMsg = Message(time, source, mName, level, msg);
    onLogMsg(logMsg, logEnabled, trackbackEnabled);
  }

  void log(const Source& source, const Level level, const std::string_view msg) {
    const bool logEnabled = shouldLog(level);
    const bool trackbackEnabled = mTracer.enabled();
    if (!logEnabled && !trackbackEnabled) {
      return;
    }

    const auto logMsg = Message(source, mName, level, msg);
    onLogMsg(logMsg, logEnabled, trackbackEnabled);
  }

  void log(const Level level, const std::string_view msg) {
    log(Source(), level, msg);
  }

  template<typename... Args>
  void trace(format_string_t<Args...> fmt, Args&&... args) {
    log(Level::kTrace, fmt, std::forward<Args>(args)...);
  }

  template<typename... Args>
  void debug(format_string_t<Args...> fmt, Args&&... args) {
    log(Level::kDebug, fmt, std::forward<Args>(args)...);
  }

  template<typename... Args>
  void info(format_string_t<Args...> fmt, Args&&... args) {
    log(Level::kInfo, fmt, std::forward<Args>(args)...);
  }

  template<typename... Args>
  void warn(format_string_t<Args...> fmt, Args&&... args) {
    log(Level::kWarn, fmt, std::forward<Args>(args)...);
  }

  template<typename... Args>
  void error(format_string_t<Args...> fmt, Args&&... args) {
    log(Level::kError, fmt, std::forward<Args>(args)...);
  }

  template<typename... Args>
  void critical(format_string_t<Args...> fmt, Args&&... args) {
    log(Level::kCritical, fmt, std::forward<Args>(args)...);
  }

  template<typename T>
  void trace(const T& msg) {
    log(Level::kTrace, msg);
  }

  template<typename T>
  void debug(const T& msg) {
    log(Level::kDebug, msg);
  }

  template<typename T>
  void info(const T& msg) {
    log(Level::kInfo, msg);
  }

  template<typename T>
  void warn(const T& msg) {
    log(Level::kWarn, msg);
  }

  template<typename T>
  void error(const T& msg) {
    log(Level::kError, msg);
  }

  template<typename T>
  void critical(const T& msg) {
    log(Level::kCritical, msg);
  }

  /**
   * Returns true if logging is enabled for the given level.
   */
  [[nodiscard]] bool shouldLog(const Level level) const { return level >= mLevel.load(std::memory_order_relaxed); }

  /**
   * Returns true if backtrace logging is enabled.
   */
  [[nodiscard]] bool shouldTrace() const { return mTracer.enabled(); }

  void setLevel(const Level newLevel) {
    mLevel.store(newLevel);
  }

  Level level() const { return mLevel.load(std::memory_order_relaxed); }

  std::string name() const { return mName; }

  void setFormatter(std::unique_ptr<Formatter> formatter);

  void setPattern(std::string pattern, TimeZone timeZone = TimeZone::kLocal);

  void enableBacktrace(const size_t nMessages) { mTracer.enable(nMessages); }

  void disableTrace() { mTracer.disable(); }

  void dumpBacktrace() { onDumpBacktrace(); }

  void flush() { onFlush(); }

  void flushLevel(const Level level) { mFlushLevel.store(level); }

  Level flushLevel() const { return mFlushLevel.load(std::memory_order_relaxed); }

  const std::vector<std::shared_ptr<Sink>>& sinks() const { return mSinks; }

  std::vector<std::shared_ptr<Sink>>& sinks() { return mSinks; }

  /**
   * Replaces every sink of this logger.
   *
   * The sinks that are replaced are flushed and then released, which closes the log files they own - the RAII
   * path out of a reconfiguration. The logger must not be logging on another thread while it is reconfigured;
   * a logger is set up at start-up.
   */
  void setSinks(std::vector<std::shared_ptr<Sink>> sinks);

  /**
   * Creates new logger with same sinks and configuration.
   */
  virtual std::shared_ptr<Logger> clone(std::string name);

  void setErrorHandler(ErrorHandler handler);

  friend void swap(Logger& lhs, Logger& rhs) noexcept {
    lhs.swap(rhs);
  }

protected:
  std::string mName;
  std::vector<std::shared_ptr<Sink>> mSinks;
  std::atomic<Level> mLevel = Level::kTrace;
  std::atomic<Level> mFlushLevel = Level::kOff;
  ErrorHandler mCustomErrHandler;
  BackTracer mTracer;

  template<typename... Args>
  void onLog(const Source source, const Level level, const std::string_view fmt, Args&&... args) {
    const bool logEnabled = shouldLog(level);
    const bool trackbackEnabled = mTracer.enabled();
    if (!logEnabled && !trackbackEnabled) {
      return;
    }
    std::string buf;
    std::vformat_to(std::back_inserter(buf), fmt, std::make_format_args(args...));

    const auto msg = Message(source, mName, level, std::string_view(buf.data(), buf.size()));
    onLogMsg(msg, logEnabled, trackbackEnabled);
  }

  // log the given message (if the given log level is high enough), and save backtrace (if backtrace is enabled).
  void onLogMsg(const Message& msg, bool logEnabled, bool trackbackEnabled);

  virtual void onSink(const Message& msg);

  virtual void onFlush();

  void onDumpBacktrace();

  bool shouldFlush(const Message& msg) const {
    return (msg.level >= flushLevel()) && (msg.level != Level::kOff);
  }

  // handle errors during logging.
  // default handler prints the error to stderr at max rate of 1 message/sec.
  void onHandleError(const std::string& msg) const;
};

} // namespace minilog

#endif // MINILOG_LOGGER_H
