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

#ifndef MINILOG_SINKS_FILE_SINK_H
#define MINILOG_SINKS_FILE_SINK_H

#include <minilog/common.h>
#include <minilog/sinks/BaseSink.h>

#include "private/minilog/FileHelper.h"
#include "private/minilog/mutex.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace minilog {

/** The file name that makes a file sink write into stdout instead of a file. */
inline constexpr std::string_view kStdOutFile = details::kStdOutFile;

/** The file name that makes a file sink write into stderr instead of a file. */
inline constexpr std::string_view kStdErrFile = details::kStdErrFile;

/**
 * Writes every formatted message to a file.
 *
 * The sink owns the file: the stream is opened by the constructor, flushed by flush() and always closed by the
 * destructor, so a logger that drops its sinks releases the file immediately and a rotation of the caller can
 * rename it right away.
 *
 * @tparam Mutex std::mutex for a sink shared by several threads, NoMutex for a sink used by one thread only.
 */
template<typename Mutex>
class BasicFileSink : public BaseSink<Mutex> {
public:
  /**
   * Opens the log file.
   *
   * @param filePath Path of the log file (UTF-8), or kStdOutFile/kStdErrFile to write into a process stream.
   * @param truncate Truncate an existing file instead of appending to it.
   * @param flushOnLog Flush the file after every message, at the price of a system call per message.
   *
   * @throws MiniLogException when the file cannot be opened, so a broken configuration fails at start-up
   *         instead of silently losing the log.
   */
  explicit BasicFileSink(const std::string& filePath, const bool truncate = false, const bool flushOnLog = false)
    : mFlushOnLog(flushOnLog) {
    mFile.open(filePath, truncate);
  }

  ~BasicFileSink() override = default;

  BasicFileSink(const BasicFileSink&) = delete;
  BasicFileSink& operator=(const BasicFileSink&) = delete;

  /**
   * Flushes and closes the current file, then opens the same name again.
   *
   * Used after the file was rotated or removed from the outside.
   */
  void reopen(const bool truncate = false) {
    std::lock_guard<Mutex> lock(this->mMutex);
    mFile.reopen(truncate);
  }

  /** Flushes the file after every message, at the price of a system call per message. */
  void setFlushOnLog(const bool flushOnLog) {
    std::lock_guard<Mutex> lock(this->mMutex);
    mFlushOnLog = flushOnLog;
  }

  [[nodiscard]] bool flushOnLog() const {
    std::lock_guard<Mutex> lock(this->mMutex);
    return mFlushOnLog;
  }

  /** Name the sink writes into; "stdout"/"stderr" for a process stream. */
  [[nodiscard]] std::string filename() const {
    std::lock_guard<Mutex> lock(this->mMutex);
    return mFile.filename();
  }

  /** Size in bytes of everything written into the file since it was opened. */
  [[nodiscard]] size_t size() const {
    std::lock_guard<Mutex> lock(this->mMutex);
    return mFile.size();
  }

protected:
  details::FileHelper mFile;
  bool mFlushOnLog{ false };

  void onSink(const Message& msg) override {
    std::string formatted;
    this->mFormatter->format(msg, formatted);
    onWrite(msg, formatted);
  }

  /**
   * Writes a formatted message; the sink mutex is held for the whole call.
   *
   * The hook lets a derived sink act on the message (a rotation, for instance) before it reaches the file.
   */
  virtual void onWrite(const Message& /*msg*/, const std::string_view formatted) {
    mFile.write(formatted);

    if (mFlushOnLog) {
      mFile.flush();
    }
  }

  void onFlush() override {
    // A stream that is already empty must not turn a flush into an error, so a closed helper is accepted here.
    if (mFile.isOpen()) {
      mFile.flush();
    }
  }
};

/** File sink for loggers shared by several threads. */
using FileSinkMt = BasicFileSink<std::mutex>;

/** File sink for a logger that lives on a single thread. */
using FileSinkSt = BasicFileSink<NoMutex>;

/** The file sink used by default. */
using FileSink = FileSinkMt;

} // namespace minilog

#endif // MINILOG_SINKS_FILE_SINK_H
