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

#include <minilog/log.h>

#include <minilog/PatternFormatter.h>

#ifdef __ANDROID__
#include <minilog/sinks/AndroidSink.h>
#elif defined(_WIN32)
#include <minilog/sinks/WinColorSink.h>
#else
#include <minilog/sinks/AnsiColorSink.h>
#endif

#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <unordered_map>

namespace minilog {
namespace details {

namespace {

/**
 * The logger the log macros fall back on.
 *
 * It is named "" and writes into the console of the platform through the default sink.
 */
std::shared_ptr<Logger> makePlatformDefaultLogger() {
  std::shared_ptr<Sink> sink;
#if defined(__ANDROID__)
  sink = std::make_shared<AndroidDefaultSink>();
#elif defined(_WIN32)
  sink = std::make_shared<WinColorDefaultSink>();
#else
  sink = std::make_shared<AnsiColorDefaultSink>();
#endif
  return std::make_shared<Logger>("", std::move(sink));
}

} // namespace anonymous

class Registry {
public:
  Registry(const Registry&) = delete;
  Registry& operator=(const Registry&) = delete;

  void registerLogger(std::shared_ptr<Logger> logger);

  void registerOrReplace(std::shared_ptr<Logger> logger);

  void initialLogger(std::shared_ptr<Logger> logger);

  std::shared_ptr<Logger> getLogger(const std::string& name);

  /** Never returns null: the default logger is re-created when it was dropped. */
  std::shared_ptr<Logger> defaultLogger();

  void setDefaultLogger(std::shared_ptr<Logger> logger);

  void setFormatter(std::unique_ptr<Formatter> formatter);

  void enableBacktrace(size_t numMessages);

  void disableBacktrace();

  void setLevel(Level level);

  void flushLevel(Level level);

  void flushAll();

  void drop(const std::string& name);

  void dropAll();

  static Registry& instance();

private:
  Registry();
  ~Registry();

  std::unordered_map<std::string, std::shared_ptr<Logger>> mLoggers;

  std::mutex mMutex;

  std::unique_ptr<Formatter> mFormatter;
  Level mGlobalLevel = Level::kInfo;
  Level mFlushLevel = Level::kOff;
  ErrorHandler mErrorHandler;

  std::unordered_map<std::string, Level> mLevels{};

  std::shared_ptr<Logger> mDefaultLogger;

  bool mAutoRegistration = true;
  size_t mNumBacktraces = 0;

  /** The default logger of the platform, with the global formatter applied. */
  std::shared_ptr<Logger> createDefaultLogger();

  void onRegisterLogger(std::shared_ptr<Logger> logger);

  void onRegisterOrReplace(std::shared_ptr<Logger> logger);
};

Registry& Registry::instance() {
  static Registry sInstance;
  return sInstance;
}

Registry::Registry() : mFormatter(std::make_unique<PatternFormatter>()) {
  mDefaultLogger = createDefaultLogger();
  mLoggers[mDefaultLogger->name()] = mDefaultLogger;
}

Registry::~Registry() = default;

std::shared_ptr<Logger> Registry::createDefaultLogger() {
  const auto logger = makePlatformDefaultLogger();

  // The global formatter, but not the global level: the level of a logger defaults to trace, and a message that
  // is written before the application configured the logging must not be filtered away by surprise. The flush
  // level and the backtrace are safe to apply, and a logger re-created after dropAll() must keep them.
  logger->setFormatter(mFormatter->clone());
  logger->flushLevel(mFlushLevel);
  if (mNumBacktraces > 0) {
    logger->enableBacktrace(mNumBacktraces);
  }
  if (mErrorHandler) {
    logger->setErrorHandler(mErrorHandler);
  }
  return logger;
}

std::shared_ptr<Logger> Registry::defaultLogger() {
  std::lock_guard<std::mutex> lock(mMutex);

  if (mDefaultLogger == nullptr) {
    // dropAll() removes the default logger; the log macros call this on every message, so it is re-created on
    // demand instead of ever handing out a null logger.
    mDefaultLogger = createDefaultLogger();
    mLoggers[mDefaultLogger->name()] = mDefaultLogger;
  }

  return mDefaultLogger;
}

void Registry::registerLogger(std::shared_ptr<Logger> logger) {
  std::lock_guard<std::mutex> lock(mMutex);
  onRegisterLogger(std::move(logger));
}

void Registry::registerOrReplace(std::shared_ptr<Logger> logger) {
  std::lock_guard<std::mutex> lock(mMutex);
  onRegisterOrReplace(std::move(logger));
}

void Registry::initialLogger(std::shared_ptr<Logger> logger) {
  std::lock_guard<std::mutex> lock(mMutex);
  logger->setFormatter(mFormatter->clone());

  if (mErrorHandler) {
    logger->setErrorHandler(mErrorHandler);
  }

  // set new level according to previously configured level or default level
  const auto it = mLevels.find(logger->name());
  const auto newLevel = (it != mLevels.end()) ? it->second : mGlobalLevel;
  logger->setLevel(newLevel);
  // The flush level is a registry setting of its own; using the log level here made every future logger
  // flush on every message and silently ignored the configured flush level.
  logger->flushLevel(mFlushLevel);

  if (mNumBacktraces > 0) {
    logger->enableBacktrace(mNumBacktraces);
  }

  if (mAutoRegistration) {
    onRegisterLogger(std::move(logger));
  }
}

std::shared_ptr<Logger> Registry::getLogger(const std::string& name) {
  std::lock_guard<std::mutex> lock(mMutex);
  const auto found = mLoggers.find(name);
  return found == mLoggers.end() ? nullptr : found->second;
}

void Registry::setDefaultLogger(std::shared_ptr<Logger> logger) {
  std::lock_guard<std::mutex> lock(mMutex);

  if (logger == nullptr) {
    // Forget the current default logger; the next log call creates a fresh one. The mutex stays locked, so the
    // default logger is erased directly instead of going through defaultLogger(), which locks again.
    if (mDefaultLogger != nullptr) {
      mLoggers.erase(mDefaultLogger->name());
      mDefaultLogger.reset();
    }
    return;
  }

  // A logger is keyed by its name in the registry, so the entry of the logger that is replaced is removed
  // rather than left behind under a name nobody can reach any more.
  if (mDefaultLogger != nullptr) {
    mLoggers.erase(mDefaultLogger->name());
  }

  mDefaultLogger = std::move(logger);
  mLoggers[mDefaultLogger->name()] = mDefaultLogger;
}

void Registry::setFormatter(std::unique_ptr<Formatter> formatter) {
  if (formatter == nullptr) {
    // A null formatter would make the clone below crash; the sinks refuse an empty formatter for the same
    // reason, so the global setter refuses it as well.
    return;
  }

  std::lock_guard<std::mutex> lock(mMutex);
  mFormatter = std::move(formatter);
  for (const auto& logger: mLoggers | std::views::values) {
    logger->setFormatter(mFormatter->clone());
  }
}

void Registry::enableBacktrace(const size_t numMessages) {
  std::lock_guard<std::mutex> lock(mMutex);
  mNumBacktraces = numMessages;
  for (const auto& logger: mLoggers | std::views::values) {
    logger->enableBacktrace(numMessages);
  }
}

void Registry::disableBacktrace() {
  std::lock_guard<std::mutex> lock(mMutex);
  mNumBacktraces = 0;
  for (const auto& logger: mLoggers | std::views::values) {
    logger->disableTrace();
  }
}

void Registry::setLevel(const Level level) {
  std::lock_guard<std::mutex> lock(mMutex);
  for (const auto& logger: mLoggers | std::views::values) {
    logger->setLevel(level);
  }
  mGlobalLevel = level;
}

void Registry::flushLevel(const Level level) {
  std::lock_guard<std::mutex> lock(mMutex);
  for (const auto& logger: mLoggers | std::views::values) {
    logger->flushLevel(level);
  }
  mFlushLevel = level;
}

void Registry::flushAll() {
  std::lock_guard<std::mutex> lock(mMutex);
  for (const auto& logger: mLoggers | std::views::values) {
    logger->flush();
  }
}

void Registry::drop(const std::string& name) {
  std::lock_guard<std::mutex> lock(mMutex);
  const auto isDefaultLogger = mDefaultLogger && mDefaultLogger->name() == name;
  mLoggers.erase(name);
  if (isDefaultLogger) {
    mDefaultLogger.reset();
  }
}

void Registry::dropAll() {
  std::lock_guard<std::mutex> lock(mMutex);
  mLoggers.clear();
  mDefaultLogger.reset();
}

void Registry::onRegisterLogger(std::shared_ptr<Logger> logger) {
  const auto name = logger->name();
  if (mLoggers.contains(name)) {
    MINILOG_THROW(MiniLogException("logger with name '" + name + "' already exists"));
  }
  mLoggers[name] = std::move(logger);
}

void Registry::onRegisterOrReplace(std::shared_ptr<Logger> logger) {
  const auto name = logger->name();

  // Replacing the logger that happens to be the default one must move the default pointer with it; otherwise
  // defaultLogger() keeps returning the replaced logger while getLogger(name) returns the new one.
  if (mDefaultLogger != nullptr && mDefaultLogger->name() == name) {
    mDefaultLogger = logger;
  }

  mLoggers[name] = std::move(logger);
}

} // namespace details

void initializeLogger(std::shared_ptr<Logger> logger) {
  // initialLogger() applies the global settings - formatter, level, backtrace - before it registers the logger.
  details::Registry::instance().initialLogger(std::move(logger));
}

std::shared_ptr<Logger> getLogger(const std::string& name) {
  return details::Registry::instance().getLogger(name);
}

void setFormatter(std::unique_ptr<Formatter> formatter) {
  details::Registry::instance().setFormatter(std::move(formatter));
}

void setPattern(std::string pattern, const TimeZone timeZone) {
  setFormatter(std::make_unique<PatternFormatter>(std::move(pattern), timeZone));
}

void setLevel(const Level level) {
  details::Registry::instance().setLevel(level);
}

void enableBacktrace(const size_t n_messages) {
  details::Registry::instance().enableBacktrace(n_messages);
}

void disableBacktrace() {
  details::Registry::instance().disableBacktrace();
}

void dumpBacktrace() {
  defaultLogger()->dumpBacktrace();
}

void flushLevel(const Level level) {
  details::Registry::instance().flushLevel(level);
}

void flushAll() {
  details::Registry::instance().flushAll();
}

void dropAll() {
  details::Registry::instance().dropAll();
}

void shutdown() {
  flushAll();
  dropAll();
}

void registerLogger(std::shared_ptr<Logger> logger) {
  details::Registry::instance().registerLogger(std::move(logger));
}

void registerOrReplace(std::shared_ptr<Logger> logger) {
  details::Registry::instance().registerOrReplace(std::move(logger));
}

std::shared_ptr<Logger> defaultLogger() {
  return details::Registry::instance().defaultLogger();
}

void setDefaultLogger(std::shared_ptr<Logger> logger) {
  details::Registry::instance().setDefaultLogger(std::move(logger));
}

} // namespace minilog
