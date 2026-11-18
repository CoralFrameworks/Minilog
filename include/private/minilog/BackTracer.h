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

#ifndef MINILOG_BACK_TRACER_H
#define MINILOG_BACK_TRACER_H

#include "private/minilog/Message.h"
#include "private/minilog/MessageQueue.h"

#include <atomic>
#include <functional>
#include <mutex>

namespace minilog {

/**
 * Store log messages in circular buffer.
 * Useful for storing debug data in case of error/warning happens.
 */
class BackTracer {
public:
  BackTracer() = default;

  BackTracer(const BackTracer& other);
  BackTracer(BackTracer&& other) noexcept;
  BackTracer& operator=(const BackTracer& other) noexcept;

  /**
   * Starts recording the last `size` messages.
   *
   * A size of zero disables the backtrace: a queue without slots could not record anything and must not be
   * pushed into.
   */
  void enable(size_t size);

  void disable() {
    std::lock_guard<std::mutex> lock{ mMutex };
    mEnabled.store(false, std::memory_order_relaxed);
  }

  bool enabled() const { return mEnabled.load(std::memory_order_relaxed); }

  void push_back(const Message& msg);

  bool empty() const {
    std::lock_guard<std::mutex> lock{ mMutex };
    return mMessageQueue.empty();
  }

  /**
   * pop all items in the q and apply the given fun on each of them.
   */
  void foreach_pop(const std::function<void(const Message&)>& func);

private:
  mutable std::mutex mMutex;
  std::atomic<bool> mEnabled{ false };
  MessageQueue<MessageBuffer> mMessageQueue;
};

} // namespace minilog

#endif //MINILOG_BACK_TRACER_H
