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

#ifndef MINILOG_FILE_HELPER_H
#define MINILOG_FILE_HELPER_H

#include <minilog/common.h>

#include "private/minilog/noncopyable.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace minilog::details {

/**
 * The file names that make FileHelper write to one of the process streams instead of a file.
 *
 * A helper opened with one of these names never closes the stream it writes into: the stream belongs to the
 * runtime, not to the sink.
 */
inline constexpr std::string_view kStdOutFile = "stdout";
inline constexpr std::string_view kStdErrFile = "stderr";

/**
 * RAII owner of the C stream a file sink writes into.
 *
 * Ownership rules:
 *
 *  - the stream opened by open() is owned by the helper and is always closed by the destructor, by close() or
 *    by the next open(), so a sink can never leak a file handle;
 *  - the "stdout" and "stderr" streams are borrowed and are never closed;
 *  - the helper is not copyable and not movable, exactly like the sink that owns it, so the handle cannot be
 *    duplicated and closed twice;
 *  - open() releases the previous stream before it creates the new one, therefore a failing open cannot leave
 *    the helper in a half-opened state: it either holds the new stream or no stream at all.
 *
 * Every method except close() reports a failure by throwing MiniLogException. The logger catches it, reports
 * it through the error handler and keeps the process alive - see Logger::onSink().
 */
class FileHelper final : public noncopyable {
public:
  FileHelper() = default;

  ~FileHelper() { close(); }

  /**
   * Opens the file, releasing whatever was open before.
   *
   * @param filePath Path of the log file, UTF-8 encoded. kStdOutFile and kStdErrFile select a process stream.
   * @param truncate Truncate an existing file instead of appending to it.
   *
   * @throws MiniLogException when the file cannot be opened.
   */
  void open(const std::string& filePath, bool truncate);

  /**
   * Opens the current file again, keeping its name.
   *
   * Used after the file was renamed or removed from the outside (by a rotation, for instance).
   *
   * @throws MiniLogException when no file was opened yet or the file cannot be opened.
   */
  void reopen(bool truncate);

  /**
   * Flushes and closes the owned stream; a borrowed stream (stdout/stderr) is left untouched.
   *
   * Never throws: it is called from the destructor. The stream of a closed helper is flushed by the runtime.
   */
  void close() noexcept;

  /**
   * Flushes everything that is still buffered in the stream to the file system.
   *
   * @throws MiniLogException when the flush fails or the helper holds no stream.
   */
  void flush();

  /**
   * Writes the whole buffer to the stream and keeps the current size up to date.
   *
   * @throws MiniLogException when the helper holds no stream or the write fails (a full disk, for instance).
   */
  void write(std::string_view buffer);

  /** Size in bytes of the file, as written through this helper since it was opened. */
  [[nodiscard]] size_t size() const noexcept { return mSize; }

  [[nodiscard]] bool isOpen() const noexcept { return mFile != nullptr; }

  /** True when the helper owns the stream, false when it writes into stdout/stderr. */
  [[nodiscard]] bool isOwned() const noexcept { return mOwned; }

  /** The borrowed stream, for a sink that has to poll it (an invalid handle of a GUI process, for instance). */
  [[nodiscard]] std::FILE* file() const noexcept { return mFile; }

  /** Name the helper was opened with, empty after close(). */
  [[nodiscard]] const std::string& filename() const noexcept { return mFilename; }

private:
  std::FILE* mFile{ nullptr };
  std::string mFilename;
  size_t mSize{ 0 };
  bool mOwned{ false };

  /** Reads the current size of the file the stream is attached to. */
  void refreshSize() noexcept;
};

} // namespace minilog::details

#endif // MINILOG_FILE_HELPER_H
