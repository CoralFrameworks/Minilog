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

#include "private/minilog/FileHelper.h"

#include "private/minilog/Utils.h"

#include <cerrno>
#include <cstdint>

namespace minilog::details {

namespace {

/**
 * The position of the stream in bytes, or -1.
 *
 * ftell() is not used on Windows because a long is 32 bits there and a log file may pass the 2 GB mark before
 * the rotation takes over.
 */
int64_t streamPosition(std::FILE* stream) noexcept {
#if defined(_WIN32)
  return static_cast<int64_t>(::_ftelli64(stream));
#else
  return static_cast<int64_t>(::ftello(stream));
#endif
}

} // namespace anonymous

void FileHelper::open(const std::string& filePath, const bool truncate) {
  // RAII: the previous stream is released before the new one is created. A failing open therefore cannot leak
  // the old handle and cannot leave the helper half opened.
  close();

  std::FILE* stream = nullptr;
  bool owned = true;

  if (filePath == kStdOutFile) {
    stream = stdout;
    owned = false;
  } else if (filePath == kStdErrFile) {
    stream = stderr;
    owned = false;
  } else {
    if (filePath.empty()) {
      throw MiniLogException("failed opening log file: the path is empty");
    }

    // Binary mode on purpose: the pattern formatter already appends the end of line of the platform, so the
    // runtime must not translate it a second time (which would turn "\r\n" into "\r\r\n" on Windows).
    stream = Utils::openFile(filePath, truncate ? "wb" : "ab");
    if (stream == nullptr) {
      throw MiniLogException("failed opening log file '" + filePath + "'", errno);
    }
  }

  mFile = stream;
  mFilename = filePath;
  mOwned = owned;
  refreshSize();
}

void FileHelper::reopen(const bool truncate) {
  if (mFilename.empty()) {
    throw MiniLogException("failed reopening log file: no file was opened before");
  }

  // A copy, because open() clears the name before it stores it again.
  const auto filePath = mFilename;
  open(filePath, truncate);
}

void FileHelper::close() noexcept {
  if (mFile != nullptr && mOwned) {
    // fclose() flushes the buffer and releases the handle. A failure cannot be reported from a destructor, so
    // it is deliberately ignored here; flush() is the checked variant.
    (void)std::fclose(mFile);
  }

  mFile = nullptr;
  mFilename.clear();
  mSize = 0;
  mOwned = false;
}

void FileHelper::flush() {
  if (mFile == nullptr) {
    throw MiniLogException("failed flushing log file: no file is open");
  }

  if (std::fflush(mFile) != 0) {
    throw MiniLogException("failed flushing log file '" + mFilename + "'", errno);
  }
}

void FileHelper::write(const std::string_view buffer) {
  if (mFile == nullptr) {
    throw MiniLogException("failed writing to log file: no file is open");
  }

  if (buffer.empty()) {
    return;
  }

  // One fwrite() call per message: the stream itself is locked for the duration of the call, which keeps a
  // single line atomic even when several processes log into the same file.
  const auto written = std::fwrite(buffer.data(), 1, buffer.size(), mFile);
  if (written != buffer.size()) {
    throw MiniLogException("failed writing to log file '" + mFilename + "'", errno);
  }

  mSize += buffer.size();
}

void FileHelper::refreshSize() noexcept {
  mSize = 0;

  if (mFile == nullptr || !mOwned) {
    // stdout and stderr have no size that could drive a rotation.
    return;
  }

  // Seek to the end to learn how much an append-mode open already contains. The stream is in append mode, so
  // the seek does not influence where the next write goes.
  if (std::fseek(mFile, 0, SEEK_END) != 0) {
    return;
  }

  const auto position = streamPosition(mFile);
  if (position > 0) {
    mSize = static_cast<size_t>(position);
  }
}

} // namespace minilog::details
