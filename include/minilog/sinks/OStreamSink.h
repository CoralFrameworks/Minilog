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

#ifndef MINILOG_SINKS_OSTREAM_SINK_H
#define MINILOG_SINKS_OSTREAM_SINK_H

#include <minilog/common.h>
#include <minilog/sinks/BaseSink.h>

#include "private/minilog/mutex.h"

#include <mutex>
#include <ostream>
#include <string>

namespace minilog {

/**
 * Writes the formatted messages into a std::ostream.
 *
 * The stream is borrowed: the sink flushes it but never closes or destroys it, so a stream that outlives the
 * sink (std::cout, a file stream owned by the caller) keeps working after the sink is gone. A std::ostringstream
 * makes the sink usable to capture the log of a test.
 *
 * @tparam Mutex std::mutex for a sink shared by several threads, NoMutex for a sink used by one thread only.
 */
template<typename Mutex>
class OStreamSink final : public BaseSink<Mutex> {
public:
  /**
   * @param stream Stream the messages are written into; it must outlive the sink.
   * @param flushOnLog Flush the stream after every message. std::cout is line buffered already, a string stream
   *        needs no flush at all, so this is only useful for a stream that buffers on its own.
   */
  explicit OStreamSink(std::ostream& stream, const bool flushOnLog = false) noexcept
    : mStream(stream),
      mFlushOnLog(flushOnLog) {
  }

  ~OStreamSink() override = default;

  OStreamSink(const OStreamSink&) = delete;
  OStreamSink& operator=(const OStreamSink&) = delete;

  void setFlushOnLog(const bool flushOnLog) {
    std::lock_guard<Mutex> lock(this->mMutex);
    mFlushOnLog = flushOnLog;
  }

  [[nodiscard]] bool flushOnLog() const {
    std::lock_guard<Mutex> lock(this->mMutex);
    return mFlushOnLog;
  }

protected:
  void onSink(const Message& msg) override {
    std::string formatted;
    this->mFormatter->format(msg, formatted);
    mStream.write(formatted.data(), static_cast<std::streamsize>(formatted.size()));

    if (mFlushOnLog) {
      mStream.flush();
    }
  }

  void onFlush() override { mStream.flush(); }

private:
  std::ostream& mStream;
  bool mFlushOnLog{ false };
};

/** Stream sink for loggers shared by several threads. */
using OStreamSinkMt = OStreamSink<std::mutex>;

/** Stream sink for a logger that lives on a single thread. */
using OStreamSinkSt = OStreamSink<NoMutex>;

/** The stream sink used by default. */
using OStreamSinkDefault = OStreamSinkMt;

} // namespace minilog

#endif // MINILOG_SINKS_OSTREAM_SINK_H
