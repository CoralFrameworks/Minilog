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

#ifndef MINILOG_SINKS_DIST_SINK_H
#define MINILOG_SINKS_DIST_SINK_H

#include <minilog/Sink.h>
#include <minilog/common.h>
#include <minilog/sinks/BaseSink.h>

#include "private/minilog/mutex.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace minilog {

/**
 * Forwards every message to all the sinks it holds.
 *
 * This is the sink that combines the outputs of a debugging session: one console sink for the developer, one
 * MSVC sink for the debugger of Visual Studio, one file sink that survives the process. The pattern and the
 * level of the dist sink are handed down to every contained sink.
 *
 * The contained sinks are owned through std::shared_ptr, so a sink can be shared between several loggers, and
 * the last owner releases it - which closes a log file right away.
 *
 * Adding or removing a sink while the logger is logging, and in particular from the callback of one of the
 * contained sinks, is not supported: the sink mutex is not recursive.
 *
 * @tparam Mutex std::mutex for a sink shared by several threads, NoMutex for a sink used by one thread only.
 */
template<typename Mutex>
class DistSink final : public BaseSink<Mutex> {
public:
  using SinkPtr = std::shared_ptr<Sink>;

  DistSink() = default;

  explicit DistSink(std::vector<SinkPtr> sinks) {
    for (auto& sink: sinks) {
      addSink(std::move(sink));
    }
  }

  ~DistSink() override = default;

  DistSink(const DistSink&) = delete;
  DistSink& operator=(const DistSink&) = delete;

  /**
   * Adds a sink.
   *
   * @throws MiniLogException when the sink is empty, or when it is this dist sink itself, which would recurse
   *         for ever.
   */
  void addSink(SinkPtr sink) {
    if (sink == nullptr) {
      throw MiniLogException("cannot add an empty sink to a dist sink");
    }
    if (sink.get() == static_cast<Sink*>(this)) {
      throw MiniLogException("a dist sink cannot contain itself");
    }

    std::lock_guard<Mutex> lock(this->mMutex);
    mSinks.push_back(std::move(sink));
  }

  /** Removes a sink; removing a sink that is not there is a no-op. */
  void removeSink(const SinkPtr& sink) {
    std::lock_guard<Mutex> lock(this->mMutex);
    // The erase-remove idiom rather than std::erase: it is available on every standard library of the project.
    mSinks.erase(std::remove(mSinks.begin(), mSinks.end(), sink), mSinks.end());
  }

  /** Removes every sink, releasing the ones nobody else holds. */
  void clearSinks() {
    std::lock_guard<Mutex> lock(this->mMutex);
    mSinks.clear();
  }

  [[nodiscard]] std::vector<SinkPtr> sinks() const {
    std::lock_guard<Mutex> lock(this->mMutex);
    return mSinks;
  }

  [[nodiscard]] size_t size() const {
    std::lock_guard<Mutex> lock(this->mMutex);
    return mSinks.size();
  }

  /** Sets the level of the dist sink and of every sink it holds. */
  void setLevel(const Level level) override {
    Sink::setLevel(level);

    std::lock_guard<Mutex> lock(this->mMutex);
    for (const auto& sink: mSinks) {
      sink->setLevel(level);
    }
  }

protected:
  // The sink mutex of the base class is held for the whole call, so the vector cannot be modified while it is
  // walked - the contained sinks have a mutex of their own, which never nests in the other direction.
  void onSink(const Message& msg) override {
    for (const auto& sink: mSinks) {
      // The logger checked the level of the dist sink already; every contained sink filters once more.
      if (sink->shouldLog(msg.level)) {
        sink->log(msg);
      }
    }
  }

  void onFlush() override {
    for (const auto& sink: mSinks) {
      sink->flush();
    }
  }

  void updateFormatter(std::unique_ptr<Formatter> formatter) override {
    if (formatter == nullptr) {
      return;
    }

    // The last sink takes the formatter itself, the ones before get a copy - the same rule the logger follows.
    for (auto it = mSinks.begin(); it != mSinks.end(); ++it) {
      if (std::next(it) == mSinks.end()) {
        (*it)->setFormatter(std::move(formatter));
      } else {
        (*it)->setFormatter(formatter->clone());
      }
    }
  }

  void updatePattern(const std::string& pattern) override {
    for (const auto& sink: mSinks) {
      sink->setPattern(pattern);
    }
  }

private:
  std::vector<SinkPtr> mSinks;
};

/** Dist sink for loggers shared by several threads. */
using DistSinkMt = DistSink<std::mutex>;

/** Dist sink for a logger that lives on a single thread. */
using DistSinkSt = DistSink<NoMutex>;

/** The dist sink used by default. */
using Dist = DistSinkMt;

} // namespace minilog

#endif // MINILOG_SINKS_DIST_SINK_H
