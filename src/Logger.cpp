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

#include <minilog/Logger.h>

#include <minilog/PatternFormatter.h>

#include "private/minilog/Utils.h"

#include <cstdio>

namespace minilog {

Logger::Logger(const Logger& other)
  : mName(other.name()),
    mSinks(other.mSinks),
    mLevel(other.level()),
    mFlushLevel(other.flushLevel()),
    mCustomErrHandler(other.mCustomErrHandler),
    mTracer(other.mTracer) {
}

Logger::Logger(Logger&& other) noexcept
  : mName(std::move(other.mName)),
    mSinks(std::move(other.mSinks)),
    mLevel(other.level()),
    mFlushLevel(other.flushLevel()),
    mCustomErrHandler(std::move(other.mCustomErrHandler)),
    mTracer(std::move(other.mTracer)) {
}

Logger& Logger::operator=(Logger other) noexcept {
  this->swap(other);
  return *this;
}

void Logger::swap(Logger& other) noexcept {
  mName.swap(other.mName);
  mSinks.swap(other.mSinks);

  // swap level
  auto otherLevel = other.mLevel.load();
  auto myLevel = mLevel.exchange(otherLevel);
  other.mLevel.store(myLevel);

  // swap flush level
  otherLevel = other.mFlushLevel.load();
  myLevel = mFlushLevel.exchange(otherLevel);
  other.mFlushLevel.store(myLevel);

  mCustomErrHandler.swap(other.mCustomErrHandler);

  std::swap(mTracer, other.mTracer);
}

void Logger::setFormatter(std::unique_ptr<Formatter> formatter) {
  if (formatter == nullptr) {
    // An empty formatter would be dereferenced by the clone() below for every sink but the last one; the sinks
    // refuse it too, so the logger refuses it instead of crashing half way through the sink list.
    return;
  }

  for (auto it = mSinks.begin(); it != mSinks.end(); ++it) {
    if (std::next(it) == mSinks.end()) {
      // last element - we can be moved it.
      (*it)->setFormatter(std::move(formatter));
      break; // to prevent clang-tidy warning
    } else {
      (*it)->setFormatter(formatter->clone());
    }
  }
}

void Logger::setPattern(std::string pattern, TimeZone timeZone) {
  auto formatter = std::make_unique<PatternFormatter>(std::move(pattern), timeZone);
  setFormatter(std::move(formatter));
}

void Logger::setSinks(std::vector<std::shared_ptr<Sink>> sinks) {
  // Everything the old sinks still hold is written out before they are released.
  onFlush();
  mSinks = std::move(sinks);
}

std::shared_ptr<Logger> Logger::clone(std::string name) {
  auto cloned = std::make_shared<Logger>(*this);
  cloned->mName = std::move(name);
  return cloned;
}

void Logger::setErrorHandler(ErrorHandler handler) {
  mCustomErrHandler = std::move(handler);
}

void Logger::onLogMsg(const Message& msg, const bool logEnabled, const bool trackbackEnabled) {
  if (logEnabled) {
    onSink(msg);
  }
  if (trackbackEnabled) {
    mTracer.push_back(msg);
  }
}

void Logger::onSink(const Message& msg) {
  for (const auto& sink: mSinks) {
    if (!sink->shouldLog(msg.level)) {
      continue;
    }

#if MINILOG_EXCEPTIONS_ENABLED
    // A sink that fails - a full disk, a log directory that was removed while the process runs - must not take
    // the process down. The error is reported through the error handler and the remaining sinks still get the
    // message, so one broken output never silences the others.
    try {
      sink->log(msg);
    } catch (const std::exception& ex) {
      onHandleError(ex.what());
    } catch (...) {
      onHandleError("unknown exception while writing a log message");
    }
#else
    sink->log(msg);
#endif
  }

  if (shouldFlush(msg)) {
    onFlush();
  }
}

void Logger::onFlush() {
  for (const auto& sink: mSinks) {
#if MINILOG_EXCEPTIONS_ENABLED
    try {
      sink->flush();
    } catch (const std::exception& ex) {
      onHandleError(ex.what());
    } catch (...) {
      onHandleError("unknown exception while flushing the log");
    }
#else
    sink->flush();
#endif
  }
}

void Logger::onDumpBacktrace() {
  if (mTracer.enabled() && !mTracer.empty()) {
    onSink(Message{ name(), Level::kInfo, "****************** Backtrace Start ******************" });
    mTracer.foreach_pop([this](const Message& msg) { this->onSink(msg); });
    onSink(Message{ name(), Level::kInfo, "****************** Backtrace End ******************" });
  }
}

void Logger::onHandleError(const std::string& msg) const {
  if (mCustomErrHandler) {
    mCustomErrHandler(msg);
  } else {
    using std::chrono::system_clock;
    static std::mutex mutex;
    static std::chrono::system_clock::time_point last_report_time;
    static size_t err_counter = 0;
    std::lock_guard<std::mutex> lk{ mutex };
    const auto now = system_clock::now();
    err_counter++;
    if (now - last_report_time < std::chrono::seconds(1)) {
      return;
    }
    last_report_time = now;
    const auto tm_time = Utils::localtime(system_clock::to_time_t(now));
    char date_buf[64];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M:%S", &tm_time);
#if defined(USING_R) && defined(R_R_H) // if in R environment
    REprintf("[*** LOG ERROR #%04zu ***] [%s] [%s] %s\n", err_counter, date_buf, name().c_str(), msg.c_str());
#else
    std::fprintf(stderr, "[*** LOG ERROR #%04zu ***] [%s] [%s] %s\n", err_counter, date_buf, name().c_str(),
      msg.c_str());
#endif
  }
}

} // namespace minilog
