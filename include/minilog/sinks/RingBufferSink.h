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

#ifndef MINILOG_SINKS_RING_BUFFER_SINK_H
#define MINILOG_SINKS_RING_BUFFER_SINK_H

#include <minilog/common.h>
#include <minilog/sinks/BaseSink.h>

#include "private/minilog/mutex.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace minilog {

/**
 * Keeps the last messages in memory and writes nothing out.
 *
 * The formatted messages of the last messages can be read at any time, which is what makes them available for
 * a diagnosis after something went wrong: dump them into a crash report, or show them in a dialog when the
 * playback pipeline refuses to start. The sink keeps at most `maxMessages` of them: the oldest one is dropped
 * as soon as a new one arrives, so the memory it uses is bounded and known in advance.
 *
 * The messages are stored with the pattern of the sink applied, exactly as they would have been written to a
 * file, including the end of line.
 *
 * @tparam Mutex std::mutex for a sink shared by several threads, NoMutex for a sink used by one thread only.
 */
template<typename Mutex>
class RingBufferSink final : public BaseSink<Mutex> {
public:
  /**
   * @param maxMessages Number of messages kept, greater than zero.
   *
   * @throws MiniLogException when maxMessages is zero.
   */
  explicit RingBufferSink(const size_t maxMessages = 64) : mMaxMessages(maxMessages) {
    if (maxMessages == 0) {
      throw MiniLogException("a ring buffer sink must keep at least one message");
    }
  }

  ~RingBufferSink() override = default;

  RingBufferSink(const RingBufferSink&) = delete;
  RingBufferSink& operator=(const RingBufferSink&) = delete;

  /**
   * Changes how many messages are kept; the oldest ones are dropped when the new limit is smaller.
   *
   * @throws MiniLogException when maxMessages is zero.
   */
  void setMaxMessages(const size_t maxMessages) {
    if (maxMessages == 0) {
      throw MiniLogException("a ring buffer sink must keep at least one message");
    }

    std::lock_guard<Mutex> lock(this->mMutex);
    mMaxMessages = maxMessages;
    trim();
  }

  [[nodiscard]] size_t maxMessages() const {
    std::lock_guard<Mutex> lock(this->mMutex);
    return mMaxMessages;
  }

  [[nodiscard]] size_t size() const {
    std::lock_guard<Mutex> lock(this->mMutex);
    return mMessages.size();
  }

  [[nodiscard]] bool empty() const {
    std::lock_guard<Mutex> lock(this->mMutex);
    return mMessages.empty();
  }

  /** Forgets every message that is kept. */
  void clear() {
    std::lock_guard<Mutex> lock(this->mMutex);
    mMessages.clear();
  }

  /**
   * The formatted messages, oldest first.
   *
   * @param count Number of messages to return, counted from the newest one; 0 returns all of them.
   */
  [[nodiscard]] std::vector<std::string> lastFormatted(const size_t count = 0) const {
    std::lock_guard<Mutex> lock(this->mMutex);

    const auto wanted = (count == 0 || count > mMessages.size()) ? mMessages.size() : count;
    const auto first = mMessages.end() - static_cast<std::ptrdiff_t>(wanted);
    return { first, mMessages.end() };
  }

protected:
  void onSink(const Message& msg) override {
    std::string formatted;
    this->mFormatter->format(msg, formatted);

    mMessages.push_back(std::move(formatted));
    trim();
  }

  void onFlush() override {
    // Everything is in memory already; there is nothing to flush.
  }

private:
  // A deque, because dropping the oldest message must not move the ones that stay.
  std::deque<std::string> mMessages;
  size_t mMaxMessages{ 64 };

  /** Drops the messages that no longer fit. The sink mutex must be held by the caller. */
  void trim() {
    while (mMessages.size() > mMaxMessages) {
      mMessages.pop_front();
    }
  }
};

/** Ring buffer sink for loggers shared by several threads. */
using RingBufferSinkMt = RingBufferSink<std::mutex>;

/** Ring buffer sink for a logger that lives on a single thread. */
using RingBufferSinkSt = RingBufferSink<NoMutex>;

/** The ring buffer sink used by default. */
using RingBufferSinkDefault = RingBufferSinkMt;

} // namespace minilog

#endif // MINILOG_SINKS_RING_BUFFER_SINK_H
