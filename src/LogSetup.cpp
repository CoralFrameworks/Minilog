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

#include <minilog/LogSetup.h>

#include <minilog/log.h>
#include <minilog/sinks/DailyFileSink.h>
#include <minilog/sinks/FileSink.h>
#include <minilog/sinks/MsvcSink.h>
#include <minilog/sinks/RotatingFileSink.h>

#include "private/minilog/Utils.h"

#if defined(__ANDROID__)
#include <minilog/sinks/AndroidSink.h>
#elif defined(_WIN32)
#include <minilog/sinks/WinColorSink.h>
#else
#include <minilog/sinks/AnsiColorSink.h>
#endif

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace minilog {

namespace {

/** Applies the pattern and the level of one sink; an empty pattern keeps the default of the sink. */
void applySettings(Sink& sink, const std::string& pattern, const Level level) {
  if (!pattern.empty()) {
    sink.setPattern(pattern);
  }
  sink.setLevel(level);
}

/** The pattern of a sink: its own when it has one, the pattern of the configuration otherwise. */
const std::string& effectivePattern(const std::string& sinkPattern, const std::string& configPattern) {
  return sinkPattern.empty() ? configPattern : sinkPattern;
}

} // namespace anonymous

void setupLogging(const LoggingConfig& config) {
  std::vector<std::shared_ptr<Sink>> sinks;

  // The console of the platform.
  if (config.console.enabled) {
    std::shared_ptr<Sink> sink;
#if defined(__ANDROID__)
    sink = std::make_shared<AndroidDefaultSink>(std::string(MINILOG_TAG));
#elif defined(_WIN32)
    if (config.console.useStdErr) {
      sink = std::make_shared<WinColorErrorSink>(config.console.colorMode);
    } else {
      sink = std::make_shared<WinColorDefaultSink>(config.console.colorMode);
    }
#else
    if (config.console.useStdErr) {
      sink = std::make_shared<AnsiColorErrorSink>(config.console.colorMode);
    } else {
      sink = std::make_shared<AnsiColorDefaultSink>(config.console.colorMode);
    }
#endif
    applySettings(*sink, effectivePattern(config.console.pattern, config.pattern), config.console.level);
    sinks.push_back(std::move(sink));
  }

  // The debug output of Windows, which is what the Output window of Visual Studio shows.
#if defined(_WIN32)
  if (config.debugOutput.enabled) {
    auto sink = std::make_shared<MsvcSinkMt>(config.debugOutput.tag, config.debugOutput.checkDebuggerPresent,
      config.debugOutput.wideOutput);
    applySettings(*sink, effectivePattern(config.debugOutput.pattern, config.pattern), config.debugOutput.level);
    sinks.push_back(std::move(sink));
  }
#endif

  // The log file, which is what survives the process.
  if (config.file.enabled) {
    if (!config.file.directory.empty()) {
      std::string error;
      if (!Utils::createDirectories(config.file.directory, &error)) {
        throw MiniLogException(error);
      }
    }

    const auto filePath = Utils::joinPath(config.file.directory, config.file.basename);
    std::shared_ptr<BasicFileSink<std::mutex>> fileSink;

    if (config.file.daily) {
      fileSink = std::make_shared<DailyFileSinkMt>(filePath, config.file.dailyRotationHour,
        config.file.dailyRotationMinute, config.file.truncate);
    } else if (config.file.maxFiles > 1) {
      fileSink = std::make_shared<RotatingFileSinkMt>(filePath, config.file.maxFileSize, config.file.maxFiles,
        config.file.truncate);
    } else {
      fileSink = std::make_shared<FileSink>(filePath, config.file.truncate);
    }

    fileSink->setFlushOnLog(config.file.flushOnLog);
    applySettings(*fileSink, effectivePattern(config.file.pattern, config.pattern), config.file.level);
    sinks.push_back(std::move(fileSink));
  }

  if (sinks.empty()) {
    throw MiniLogException("the logging configuration enables no sink at all");
  }

  // The registry keeps the settings of the loggers that are created after this call.
  if (!config.pattern.empty()) {
    setPattern(config.pattern);
  }
  setLevel(config.level);
  flushLevel(config.flushLevel);

  if (config.backtraceMessages > 0) {
    enableBacktrace(config.backtraceMessages);
    defaultLogger()->enableBacktrace(config.backtraceMessages);
  } else {
    disableBacktrace();
    defaultLogger()->disableTrace();
  }

  // Replacing the sinks releases the previous ones, which flushes and closes the log files they own.
  const auto logger = defaultLogger();
  logger->setSinks(std::move(sinks));
  logger->setLevel(config.level);
  logger->flushLevel(config.flushLevel);
}

ScopedLogging::ScopedLogging(const LoggingConfig& config) {
  setupLogging(config);
}

ScopedLogging::~ScopedLogging() {
  // Flushing and dropping every logger closes the log files that are still open.
  shutdown();
}

} // namespace minilog
