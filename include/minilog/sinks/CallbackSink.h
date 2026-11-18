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

#ifndef MINILOG_SINKS_CALLBACK_SINK_H
#define MINILOG_SINKS_CALLBACK_SINK_H

#include <minilog/common.h>
#include <minilog/sinks/BaseSink.h>

#include "private/minilog/Message.h"
#include "private/minilog/mutex.h"

#include <functional>
#include <mutex>
#include <utility>

namespace minilog {

/**
 * Hands every message to a callback of the caller.
 *
 * The callback receives the message, not the formatted text, so it can decide what to do with the level, the
 * source location or the payload: forward the message to a crash reporter, count the errors, or copy it into a
 * UI. The formatting of the sink is not applied, use RingBufferSink when the formatted text is what is needed.
 *
 * The callback runs while the sink is logging, so it must not log into the same sink again (the mutex is not
 * recursive), and it must not block for long: it delays every other thread that logs.
 *
 * @tparam Mutex std::mutex for a sink shared by several threads, NoMutex for a sink used by one thread only.
 */
template<typename Mutex>
class CallbackSink final : public BaseSink<Mutex> {
public:
  using Callback = std::function<void(const Message&)>;

  /**
   * @param callback Called for every message the sink accepts; it must not be empty.
   *
   * @throws MiniLogException when the callback is empty.
   */
  explicit CallbackSink(Callback callback) {
    setCallback(std::move(callback));
  }

  ~CallbackSink() override = default;

  CallbackSink(const CallbackSink&) = delete;
  CallbackSink& operator=(const CallbackSink&) = delete;

  /**
   * Replaces the callback.
   *
   * @throws MiniLogException when the callback is empty, which would silently drop every message.
   */
  void setCallback(Callback callback) {
    if (!callback) {
      throw MiniLogException("the callback of a callback sink cannot be empty");
    }

    std::lock_guard<Mutex> lock(this->mMutex);
    mCallback = std::move(callback);
  }

  [[nodiscard]] Callback callback() const {
    std::lock_guard<Mutex> lock(this->mMutex);
    return mCallback;
  }

protected:
  void onSink(const Message& msg) override { mCallback(msg); }

  void onFlush() override {
    // The callback is called for every message; there is nothing buffered to flush.
  }

private:
  Callback mCallback;
};

/** Callback sink for loggers shared by several threads. */
using CallbackSinkMt = CallbackSink<std::mutex>;

/** Callback sink for a logger that lives on a single thread. */
using CallbackSinkSt = CallbackSink<NoMutex>;

/** The callback sink used by default. */
using CallbackSinkDefault = CallbackSinkMt;

} // namespace minilog

#endif // MINILOG_SINKS_CALLBACK_SINK_H
