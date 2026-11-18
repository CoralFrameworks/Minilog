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

#ifndef MINILOG_LOG_SETUP_H
#define MINILOG_LOG_SETUP_H

#include <minilog/common.h>

#include <cstddef>
#include <string>

namespace minilog {

/**
 * The console output of the default logger.
 *
 * The sink that is chosen depends on the platform: the Windows console sink on Windows, the ANSI sink everywhere
 * else, and the logcat sink on Android. On Android the other members only decide whether it is installed.
 */
struct ConsoleLogConfig {
  bool enabled = true;

  /** Write into stderr instead of stdout. */
  bool useStdErr = false;

  /** Colors are used when the output is a terminal, unless this is changed. */
  ColorMode colorMode = ColorMode::kAutomatic;

  /** Pattern of this sink; the pattern of LoggingConfig is used when it is empty. */
  std::string pattern{};

  /** Lowest level this sink writes. */
  Level level = Level::kTrace;
};

/**
 * The debug output of Windows (OutputDebugString), which is what the Output window of Visual Studio shows.
 *
 * It is ignored on every other platform. See MsvcSink for the details; `wideOutput` is the one to turn on when
 * the messages contain non-ASCII characters.
 */
struct DebugOutputLogConfig {
  bool enabled = true;

  /** Skip the messages while no debugger is attached to the process. */
  bool checkDebuggerPresent = true;

  /** Send the messages as UTF-16 (OutputDebugStringW) so that non-ASCII text survives. */
  bool wideOutput = false;

  /** Prefix put in front of every message, empty by default. */
  std::string tag{};

  /** Pattern of this sink; the pattern of LoggingConfig is used when it is empty. */
  std::string pattern{};

  /** Lowest level this sink writes. */
  Level level = Level::kTrace;
};

/**
 * The log file of the default logger.
 *
 * Three kinds of file are available, chosen by the members below:
 *
 *  - `daily = true` writes one file per day, named `<basename without extension>_<YYYY-MM-DD><extension>`;
 *  - `maxFiles > 1` writes a rotating file, keeping `maxFiles` generations of `maxFileSize` bytes;
 *  - otherwise a single file is written and grows without a limit.
 */
struct FileLogConfig {
  bool enabled = false;

  /** Directory the file is created in; it is created when it does not exist. Empty means the working directory. */
  std::string directory{ "logs" };

  /** Name of the file; its extension is kept in front of the date of a daily file. */
  std::string basename{ "coral.log" };

  /** Write one file per day instead of rotating by size. */
  bool daily = false;

  /** Maximum size of a rotating file in bytes, greater than zero. */
  size_t maxFileSize = 8 * 1024 * 1024;

  /** Number of files kept by a rotating file, the file that is being written included. */
  size_t maxFiles = 3;

  /** Hour of the day a daily file changes, 0-23. */
  int dailyRotationHour = 0;

  /** Minute of the hour a daily file changes, 0-59. */
  int dailyRotationMinute = 0;

  /**
   * Start a fresh file when the logger is set up: the file of the period is truncated, or - for a rotating file,
   * whose content is worth keeping - becomes the first generation.
   */
  bool truncate = false;

  /** Flush the file after every message, at the price of a system call per message. */
  bool flushOnLog = false;

  /** Pattern of this sink; the pattern of LoggingConfig is used when it is empty. */
  std::string pattern{};

  /** Lowest level this sink writes. */
  Level level = Level::kTrace;
};

/**
 * Everything setupLogging() installs on the default logger.
 *
 * Example - a debug session that keeps the console, the Visual Studio output window and a rotating log file:
 *
 *   minilog::LoggingConfig config;
 *   config.level = minilog::Level::kDebug;
 *   config.file.enabled = true;
 *   config.file.directory = "logs";
 *   config.file.basename = "coral.log";
 *   config.file.maxFiles = 5;
 *   minilog::setupLogging(config);
 *
 *   MINILOG_INFO("the playback started at {}", startTime);
 */
struct LoggingConfig {
  /** Pattern of every sink that does not override it; "%+" prints time, logger, level and source. */
  std::string pattern{ "%+" };

  /** Lowest level the loggers write. */
  Level level = Level::kTrace;

  /** Messages at this level and above flush every sink right away; kOff disables the automatic flush. */
  Level flushLevel = Level::kError;

  /** Number of messages the backtrace of the logger keeps, 0 disables the backtrace. */
  size_t backtraceMessages = 0;

  ConsoleLogConfig console{};
  DebugOutputLogConfig debugOutput{};
  FileLogConfig file{};
};

/**
 * Builds the sinks of the configuration, installs them on the default logger and applies the registry settings
 * so that a logger created later starts the same way.
 *
 * The log directory is created when it does not exist. The sinks that were installed before are flushed and
 * released, which closes the log files they own. Call it once at start-up, from a single thread: a logger is
 * reconfigured, it is not replaced while other threads log.
 *
 * @throws MiniLogException when no sink is enabled at all, when the log directory cannot be created, or when a
 *         log file cannot be opened. A broken logging configuration fails at start-up instead of quietly
 *         writing nothing.
 */
void setupLogging(const LoggingConfig& config = LoggingConfig());

/**
 * Installs the logging of the configuration on construction and shuts it down on destruction, which flushes and
 * releases every logger - the RAII way of setting up a command line tool, a test or a sample.
 *
 *   int main() {
 *     minilog::ScopedLogging logging;
 *     MINILOG_INFO("hello");
 *   }
 *
 * One of these per process: the destructor drops every logger, including the ones of the other instances.
 */
class ScopedLogging final {
public:
  explicit ScopedLogging(const LoggingConfig& config = LoggingConfig());

  ~ScopedLogging();

  ScopedLogging(const ScopedLogging&) = delete;
  ScopedLogging& operator=(const ScopedLogging&) = delete;
  ScopedLogging(ScopedLogging&&) = delete;
  ScopedLogging& operator=(ScopedLogging&&) = delete;
};

} // namespace minilog

#endif // MINILOG_LOG_SETUP_H
