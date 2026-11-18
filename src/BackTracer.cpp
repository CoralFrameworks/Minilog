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

#include "private/minilog/BackTracer.h"

namespace minilog {

BackTracer::BackTracer(const BackTracer& other) {
  std::lock_guard<std::mutex> lock(other.mMutex);
  mEnabled = other.enabled();
  mMessageQueue = other.mMessageQueue;
}

BackTracer::BackTracer(BackTracer&& other) noexcept {
  std::lock_guard<std::mutex> lock(other.mMutex);
  mEnabled = other.enabled();
  mMessageQueue = std::move(other.mMessageQueue);
}

BackTracer& BackTracer::operator=(const BackTracer& other) noexcept {
  std::lock_guard<std::mutex> lock(mMutex);
  mEnabled = other.enabled();
  mMessageQueue = other.mMessageQueue;
  return *this;
}

void BackTracer::enable(const size_t size) {
  std::lock_guard<std::mutex> lock{ mMutex };
  // A queue without slots cannot record anything, so zero means disabled rather than "enabled and empty".
  mEnabled.store(size > 0, std::memory_order_relaxed);
  mMessageQueue = MessageQueue<MessageBuffer>(size);
}

void BackTracer::push_back(const Message& msg) {
  std::lock_guard<std::mutex> lock{ mMutex };
  mMessageQueue.push_back(MessageBuffer{ msg });
}

void BackTracer::foreach_pop(const std::function<void(const Message&)>& func) {
  std::lock_guard<std::mutex> lock{ mMutex };
  while (!mMessageQueue.empty()) {
    auto& front_msg = mMessageQueue.front();
    func(front_msg);
    mMessageQueue.pop_front();
  }
}

} // namespace minilog
