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

#ifndef MINILOG_SINKS_BASE_SINK_H
#define MINILOG_SINKS_BASE_SINK_H

#include <minilog/Sink.h>

#include <minilog/PatternFormatter.h>

#include "private/minilog/noncopyable.h"

#include <memory>
#include <mutex>

namespace minilog {

/**
 * The skeleton of a sink: it owns the formatter, serialises the calls with a mutex and leaves the writing to
 * the derived class.
 *
 * @tparam Mutex std::mutex for a sink shared by several threads, NoMutex for a sink used by one thread only.
 */
template<typename Mutex>
class BaseSink : public noncopyable, public Sink {
public:
  BaseSink() : mFormatter(std::make_unique<PatternFormatter>()) {
  }

  ~BaseSink() override = default;

  explicit BaseSink(std::unique_ptr<Formatter> formatter)
    : mFormatter(formatter != nullptr ? std::move(formatter) : std::make_unique<PatternFormatter>()) {
  }

  void log(const Message &msg) override {
    std::lock_guard<Mutex> lock(mMutex);
    onSink(msg);
  }

  void flush() override {
    std::lock_guard<Mutex> lock(mMutex);
    onFlush();
  }

  void setFormatter(std::unique_ptr<Formatter> formatter) override {
    std::lock_guard<Mutex> lock(mMutex);
    updateFormatter(std::move(formatter));
  }

  void setPattern(const std::string &pattern) override {
    std::lock_guard<Mutex> lock(mMutex);
    updatePattern(pattern);
  }

protected:
  std::unique_ptr<Formatter> mFormatter;

  // Mutable so that a const query method of a derived sink can take the same lock the writers take.
  mutable Mutex mMutex;

  virtual void onSink(const Message &msg) = 0;

  virtual void onFlush() = 0;

  virtual void updatePattern(const std::string &pattern) {
    updateFormatter(std::make_unique<PatternFormatter>(pattern));
  }

  /** An empty formatter is refused: every message of the sink would fail to format. */
  virtual void updateFormatter(std::unique_ptr<Formatter> formatter) {
    if (formatter == nullptr) {
      return;
    }
    mFormatter = std::move(formatter);
  }
};

} // namespace minilog

#endif // MINILOG_SINKS_BASE_SINK_H
