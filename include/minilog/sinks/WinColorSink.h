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

#ifndef MINILOG_SINKS_WIN_COLOR_SINK_H
#define MINILOG_SINKS_WIN_COLOR_SINK_H

#include <minilog/Sink.h>

#include <minilog/PatternFormatter.h>
#include <minilog/common.h>

#include "private/minilog/mutex.h"
#include "private/minilog/noncopyable.h"

#ifdef _WIN32
#include <windows.h>

#include <wincon.h>

#include <array>
#endif
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace minilog {

/**
 * Windows color console sink. Uses WriteConsoleA to write to the console with colors
 *
 * The handle is borrowed: the sink writes into it and never closes it, so the std handles of the process stay
 * usable after the sink is gone. A null handle, or the INVALID_HANDLE_VALUE of a process that has no console,
 * makes the sink silent instead of failing.
 */
template<typename ConsoleMutex>
class WinColorSink : public noncopyable, public Sink {
public:
  explicit WinColorSink(void* outHandler, const ColorMode mode)
    : mOutHandler(outHandler),
      mMutex(ConsoleMutex::mutex()),
      mFormatter(std::make_unique<PatternFormatter>()) {
    updateColorMode(mode);

    // set level colors
    mColors[Level::kTrace] = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // white
    mColors[Level::kDebug] = FOREGROUND_GREEN | FOREGROUND_BLUE; // cyan
    mColors[Level::kInfo] = FOREGROUND_GREEN; // green
    mColors[Level::kWarn] = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; // intense yellow
    mColors[Level::kError] = FOREGROUND_RED | FOREGROUND_INTENSITY; // intense red
    mColors[Level::kCritical] = BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                                FOREGROUND_INTENSITY; // intense white on red background
    mColors[Level::kOff] = 0;
  }

  /** Restores nothing but flushes the handle; the console handle itself belongs to the process. */
  ~WinColorSink() override { release(); }

  /**
   * Changes the color for the given level
   */
  void setColor(const Level level, const std::uint16_t color) {
    std::lock_guard<mutex_t> lock(mMutex);
    const auto index = static_cast<size_t>(level);
    if (index < mColors.size()) {
      mColors[index] = color;
    }
  }

  void setColorMode(const ColorMode mode) {
    std::lock_guard<mutex_t> lock(mMutex);
    updateColorMode(mode);
  }

  void log(const Message& msg) override {
    if (!isConnected()) {
      return;
    }

    std::lock_guard<mutex_t> lock(mMutex);
    msg.colorRangeStart = 0;
    msg.colorRangeEnd = 0;
    std::string formatted;
    mFormatter->format(msg, formatted);
    if (mShouldDoColors && msg.colorRangeEnd > msg.colorRangeStart) {
      // before color range
      printRange(formatted, 0, msg.colorRangeStart);
      // in color range
      const auto origAttribs = static_cast<WORD>(setForegroundColor(levelColor(msg.level)));
      printRange(formatted, msg.colorRangeStart, msg.colorRangeEnd);
      // reset to orig colors
      ::SetConsoleTextAttribute(static_cast<HANDLE>(mOutHandler), origAttribs);
      printRange(formatted, msg.colorRangeEnd, formatted.size());
    } else {
      // print without colors if color range is invalid (or color is disabled)
      writeToFile(formatted);
    }
  }

  void flush() override {
    // The console and the file handles are written without a buffer of their own: there is nothing to flush.
  }

  /** An empty formatter is refused: every message of the sink would fail to format. */
  void setFormatter(std::unique_ptr<Formatter> formatter) override {
    if (formatter == nullptr) {
      return;
    }

    std::lock_guard<mutex_t> lock(mMutex);
    mFormatter = std::move(formatter);
  }

  void setPattern(const std::string& pattern) override {
    std::lock_guard<mutex_t> lock(mMutex);
    mFormatter = std::make_unique<PatternFormatter>(pattern);
  }

protected:
  using mutex_t = typename ConsoleMutex::mutex_t;
  void* mOutHandler;
  mutex_t& mMutex;
  bool mShouldDoColors = false;
  std::unique_ptr<Formatter> mFormatter;
  std::array<std::uint16_t, Level::kLevels> mColors{};

  /** True when the sink owns a handle it can write into. */
  [[nodiscard]] bool isConnected() const noexcept {
    return mOutHandler != nullptr && mOutHandler != INVALID_HANDLE_VALUE;
  }

  /** The color of a level, and the color of INFO for a level that has none. */
  [[nodiscard]] std::uint16_t levelColor(const Level level) const noexcept {
    const auto index = static_cast<size_t>(level);
    return index < mColors.size() ? mColors[index] : mColors[static_cast<size_t>(Level::kInfo)];
  }

  /**
   * Sets foreground color and return the orig console attributes (for resetting later)
   */
  std::uint16_t setForegroundColor(const std::uint16_t attribs) const {
    CONSOLE_SCREEN_BUFFER_INFO orig_buffer_info;
    if (!::GetConsoleScreenBufferInfo(static_cast<HANDLE>(mOutHandler), &orig_buffer_info)) {
      // just return white if failed getting console info
      return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }

    // change only the foreground bits (lowest 4 bits)
    const auto newAttribs = static_cast<WORD>(attribs) | (orig_buffer_info.wAttributes & 0xfff0);
    const auto ignored = ::SetConsoleTextAttribute(static_cast<HANDLE>(mOutHandler), static_cast<WORD>(newAttribs));
    (void) (ignored);
    return static_cast<std::uint16_t>(orig_buffer_info.wAttributes); // return orig attribs
  }

  /**
   * Prints a range of formatted message to console.
   */
  void printRange(const std::string& formatted, const size_t start, const size_t end) const {
    if (end > start && end <= formatted.size()) {
      const auto size = static_cast<DWORD>(end - start);
      const auto ignored =
        ::WriteConsoleA(static_cast<HANDLE>(mOutHandler), formatted.data() + start, size, nullptr, nullptr);
      (void) (ignored);
    }
  }

  /**
   * In case we are redirected to file (not in console mode)
   */
  void writeToFile(const std::string& formatted) const {
    const auto size = static_cast<DWORD>(formatted.size());
    DWORD bytes_written = 0;
    const auto ignored = ::WriteFile(static_cast<HANDLE>(mOutHandler), formatted.data(), size, &bytes_written, nullptr);
    (void) (ignored);
  }

  void updateColorMode(const ColorMode mode) {
    if (mode == ColorMode::kAutomatic) {
      // should do colors only if mOutHandler  points to actual console.
      DWORD console_mode;
      const bool inConsole =
        isConnected() && ::GetConsoleMode(static_cast<HANDLE>(mOutHandler), &console_mode) != 0;
      mShouldDoColors = inConsole;
    } else {
      mShouldDoColors = mode == ColorMode::kAlways ? true : false;
    }
  }

  void release() { flush(); }
};

template<typename ConsoleMutex>
class WinColorStdOutSink : public WinColorSink<ConsoleMutex> {
public:
  explicit WinColorStdOutSink(ColorMode mode = ColorMode::kAutomatic)
    : WinColorSink<ConsoleMutex>(::GetStdHandle(STD_OUTPUT_HANDLE), mode) {
  }
};

template<typename ConsoleMutex>
class WinColorStdErrSink : public WinColorSink<ConsoleMutex> {
public:
  explicit WinColorStdErrSink(ColorMode mode = ColorMode::kAutomatic)
    : WinColorSink<ConsoleMutex>(::GetStdHandle(STD_ERROR_HANDLE), mode) {
  }
};

using WinColorDefaultSink = WinColorStdOutSink<ConsoleMutex>;
using WinColorErrorSink = WinColorStdErrSink<ConsoleMutex>;

} // namespace minilog

#endif // MINILOG_SINKS_WIN_COLOR_SINK_H
