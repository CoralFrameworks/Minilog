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

#ifndef MINILOG_SINKS_ROTATING_FILE_SINK_H
#define MINILOG_SINKS_ROTATING_FILE_SINK_H

#include <minilog/common.h>
#include <minilog/sinks/FileSink.h>

#include "private/minilog/Utils.h"

#include <mutex>
#include <string>
#include <string_view>

namespace minilog {

/**
 * Writes into a file that is rotated as soon as it passes a maximum size.
 *
 * A maximum of `maxFiles` files is kept:
 *
 *  - `"logs/coral.log"` is the file that is being written;
 *  - `"logs/coral_1.log"` up to `"logs/coral_<maxFiles - 1>.log"` hold the previous generations, the highest
 *    index being the oldest one.
 *
 * Rotating closes the current file, drops the oldest one, renames every remaining generation to the next index
 * and opens a fresh file. Closing happens before the files are moved because Windows refuses to rename a file
 * that is still open, and the sink always re-opens a file even when a rename fails, so a failed rotation never
 * leaves the sink without a file to write into - the error is reported by the logger instead.
 *
 * @tparam Mutex std::mutex for a sink shared by several threads, NoMutex for a sink used by one thread only.
 */
template<typename Mutex>
class RotatingFileSink final : public BasicFileSink<Mutex> {
public:
  /**
   * Opens the base file.
   *
   * @param baseFilename Name of the file that is being written, UTF-8 encoded.
   * @param maxSize Maximum size in bytes before a rotation, greater than zero.
   * @param maxFiles Total number of files kept, including the file that is being written.
   * @param rotateOnOpen Rotate once at construction when the base file already holds something.
   *
   * @throws MiniLogException when the arguments are invalid or the file cannot be opened.
   */
  RotatingFileSink(const std::string& baseFilename, const size_t maxSize, const size_t maxFiles,
    const bool rotateOnOpen = false)
    : BasicFileSink<Mutex>(baseFilename, false),
      mBaseFilename(baseFilename),
      mMaxSize(maxSize),
      mMaxFiles(maxFiles) {
    if (maxSize == 0) {
      throw MiniLogException("rotating file sink '" + baseFilename + "': the maximum size must be greater than zero");
    }
    if (maxFiles == 0) {
      throw MiniLogException("rotating file sink '" + baseFilename + "': at least one file must be kept");
    }

    // The base sink opened the file in append mode, so this only rotates a file that is not empty, and it
    // leaves the sink with an empty file when the process starts.
    if (rotateOnOpen && this->mFile.size() > 0) {
      rotateLocked();
    }
  }

  ~RotatingFileSink() override = default;

  RotatingFileSink(const RotatingFileSink&) = delete;
  RotatingFileSink& operator=(const RotatingFileSink&) = delete;

  /**
   * Rotates the file right away.
   *
   * Must not be called from a callback that runs while this sink is logging, the mutex is not recursive.
   */
  void rotateNow() {
    std::lock_guard<Mutex> lock(this->mMutex);
    rotateLocked();
  }

  /** Maximum size in bytes before a rotation; fixed at construction. */
  [[nodiscard]] size_t maxSize() const noexcept { return mMaxSize; }

  /** Total number of files kept; fixed at construction. */
  [[nodiscard]] size_t maxFiles() const noexcept { return mMaxFiles; }

  /** Name of the file that is being written; fixed at construction. */
  [[nodiscard]] const std::string& baseFilename() const noexcept { return mBaseFilename; }

  /**
   * The name of one generation of the file.
   *
   * Index 0 is the file that is being written, every other index is inserted before the extension:
   * ("logs/coral.log", 2) -> "logs/coral_2.log".
   */
  static std::string rotatedFilename(const std::string& baseFilename, const size_t index) {
    if (index == 0) {
      return baseFilename;
    }

    return Utils::dirname(baseFilename) + Utils::stem(baseFilename) + "_" + std::to_string(index) +
           Utils::extension(baseFilename);
  }

protected:
  void onWrite(const Message& msg, const std::string_view formatted) override {
    if (shouldRotate(formatted.size())) {
      rotateLocked();
    }

    BasicFileSink<Mutex>::onWrite(msg, formatted);
  }

private:
  const std::string mBaseFilename;
  const size_t mMaxSize;
  const size_t mMaxFiles;

  [[nodiscard]] bool shouldRotate(const size_t messageSize) const noexcept {
    const auto currentSize = this->mFile.size();
    // A message larger than the whole file cannot be split, so it is written into an empty file as a whole
    // instead of triggering a rotation for every message.
    return currentSize > 0 && currentSize + messageSize > mMaxSize;
  }

  /** Rotates the file. The sink mutex must be held by the caller. */
  void rotateLocked() {
    // RAII: the owned stream is closed before the files are moved and a file is re-opened on every path out of
    // this function, including the throwing one.
    this->mFile.close();

    try {
      rotateFiles();
    } catch (...) {
      // Keep logging into the current file instead of losing the messages that follow a failed rotation.
      this->mFile.open(mBaseFilename, false);
      throw;
    }

    // The new file starts empty.
    this->mFile.open(mBaseFilename, true);
  }

  void rotateFiles() {
    std::string error;

    // The oldest generation is dropped first: it frees the index every other file moves into.
    const auto oldest = rotatedFilename(mBaseFilename, mMaxFiles - 1);
    if (Utils::fileExists(oldest) && !Utils::removeFile(oldest, &error)) {
      throw MiniLogException(error);
    }

    for (size_t index = mMaxFiles - 1; index > 0; --index) {
      const auto source = rotatedFilename(mBaseFilename, index - 1);
      if (!Utils::fileExists(source)) {
        continue;
      }

      const auto target = rotatedFilename(mBaseFilename, index);
      if (!Utils::renameFile(source, target, &error)) {
        throw MiniLogException(error);
      }
    }
  }
};

/** Rotating file sink for loggers shared by several threads. */
using RotatingFileSinkMt = RotatingFileSink<std::mutex>;

/** Rotating file sink for a logger that lives on a single thread. */
using RotatingFileSinkSt = RotatingFileSink<NoMutex>;

/** The rotating file sink used by default. */
using RotatingFile = RotatingFileSinkMt;

} // namespace minilog

#endif // MINILOG_SINKS_ROTATING_FILE_SINK_H
