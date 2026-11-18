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

#ifndef MINILOG_SINKS_ANSI_COLOR_SINK_H
#define MINILOG_SINKS_ANSI_COLOR_SINK_H

#include <minilog/Sink.h>

#include <minilog/PatternFormatter.h>

#include "private/minilog/mutex.h"
#include "private/minilog/noncopyable.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <io.h> // for _get_osfhandle, _isatty, _fileno
#include <windows.h>
#else
#include <unistd.h> // for fileno and isatty
#endif


namespace minilog {

/**
 * This sink prefixes the output with an ANSI escape sequence color code
 * depending on the severity of the message.
 *
 * If no color terminal detected, omit the escape codes.
 *
 * The stream is borrowed: the sink flushes it, on every message and when it is destroyed, but it never closes
 * it. A null stream, or the invalid stream of a process that has no console at all, is written nowhere instead
 * of tripping the invalid parameter handler of the runtime.
 */
template<typename ConsoleMutex>
class AnsiColorSink : public noncopyable, public Sink {
public:
  using mutex_t = typename ConsoleMutex::mutex_t;

  explicit AnsiColorSink(FILE* target, const ColorMode mode)
    : mTargetFile(target),
      mMutex(ConsoleMutex::mutex()),
      mFormatter(std::make_unique<PatternFormatter>()) {
    setColorMode(mode);
    mColors.at(Level::kTrace) = toString(white);
    mColors.at(Level::kDebug) = toString(cyan);
    mColors.at(Level::kInfo) = toString(green);
    mColors.at(Level::kWarn) = toString(yellow_bold);
    mColors.at(Level::kError) = toString(red_bold);
    mColors.at(Level::kCritical) = toString(bold_on_red);
    mColors.at(Level::kOff) = toString(reset);
  }

  /** Flushes what is still buffered in the borrowed stream; the stream itself is left open. */
  ~AnsiColorSink() override { flush(); }

  void setColor(const Level level, const std::string_view color) {
    std::lock_guard<mutex_t> lock(mMutex);
    mColors.at(static_cast<size_t>(level)) = toString(color);
  }

  void setColorMode(const ColorMode mode) {
    std::lock_guard<mutex_t> lock(mMutex);
    updateColorMode(mode);
  }

  bool shouldColor() const { return mShouldDoColors; }

  void log(const Message& msg) override {
    // Wrap the originally formatted message in color codes.
    // If color is not supported in the terminal, log as is instead.
    if (mTargetFile == nullptr) {
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
      printColorCode(mColors.at(static_cast<size_t>(msg.level)));
      printRange(formatted, msg.colorRangeStart, msg.colorRangeEnd);
      printColorCode(reset);
      // after color range
      printRange(formatted, msg.colorRangeEnd, formatted.size());
    } else // no color
    {
      printRange(formatted, 0, formatted.size());
    }
    fflush(mTargetFile);
  }

  void flush() override {
    std::lock_guard<mutex_t> lock(mMutex);
    if (mTargetFile != nullptr) {
      fflush(mTargetFile);
    }
  }

  void setPattern(const std::string& pattern) override {
    std::lock_guard<mutex_t> lock(mMutex);
    mFormatter = std::make_unique<PatternFormatter>(pattern);
  }

  /** An empty formatter is refused: every message of the sink would fail to format. */
  void setFormatter(std::unique_ptr<Formatter> formatter) override {
    if (formatter == nullptr) {
      return;
    }

    std::lock_guard<mutex_t> lock(mMutex);
    mFormatter = std::move(formatter);
  }

  // Formatting codes
  const std::string_view reset = "\033[m";
  const std::string_view bold = "\033[1m";
  const std::string_view dark = "\033[2m";
  const std::string_view underline = "\033[4m";
  const std::string_view blink = "\033[5m";
  const std::string_view reverse = "\033[7m";
  const std::string_view concealed = "\033[8m";
  const std::string_view clear_line = "\033[K";

  // Foreground colors
  const std::string_view black = "\033[30m";
  const std::string_view red = "\033[31m";
  const std::string_view green = "\033[32m";
  const std::string_view yellow = "\033[33m";
  const std::string_view blue = "\033[34m";
  const std::string_view magenta = "\033[35m";
  const std::string_view cyan = "\033[36m";
  const std::string_view white = "\033[37m";

  /// Background colors
  const std::string_view on_black = "\033[40m";
  const std::string_view on_red = "\033[41m";
  const std::string_view on_green = "\033[42m";
  const std::string_view on_yellow = "\033[43m";
  const std::string_view on_blue = "\033[44m";
  const std::string_view on_magenta = "\033[45m";
  const std::string_view on_cyan = "\033[46m";
  const std::string_view on_white = "\033[47m";

  /// Bold colors
  const std::string_view yellow_bold = "\033[33m\033[1m";
  const std::string_view red_bold = "\033[31m\033[1m";
  const std::string_view bold_on_red = "\033[1m\033[41m";

protected:
  // Borrowed from the caller or from the runtime, never closed by the sink.
  FILE* mTargetFile;

  mutex_t& mMutex;
  bool mShouldDoColors = false;
  std::unique_ptr<Formatter> mFormatter;
  std::array<std::string, Level::kLevels> mColors;

private:
  void updateColorMode(const ColorMode mode) {
    switch (mode) {
      case ColorMode::kAlways:
        mShouldDoColors = true;
        return;
      case ColorMode::kAutomatic:
        mShouldDoColors = isInTerminal(mTargetFile) && isColorTerminal();
        return;
      case ColorMode::kNever:
        mShouldDoColors = false;
        return;
      default:
        mShouldDoColors = false;
    }
  }

  void printColorCode(const std::string_view& colorCode) const {
    fwriteBytes(colorCode.data(), colorCode.size(), mTargetFile);
  }

  void printRange(const std::string& formatted, const size_t start, const size_t end) const {
    if (end <= start || end > formatted.size()) {
      return;
    }
    fwriteBytes(formatted.data() + start, end - start, mTargetFile);
  }

  static std::string toString(const std::string_view& sv) { return { sv.data(), sv.size() }; }

  static bool fwriteBytes(const void* ptr, const size_t bytes, FILE* fp) {
    if (fp == nullptr) {
      return false;
    }
#if defined(_WIN32)
    return _fwrite_nolock(ptr, 1, bytes, fp) == bytes;
#else
    return std::fwrite(ptr, 1, bytes, fp) == bytes;
#endif
  }

  static bool isInTerminal(FILE* fp) {
    if (fp == nullptr) {
      return false;
    }

#if defined(_WIN32)
    const auto descriptor = ::_fileno(fp);
    if (descriptor < 0) {
      return false;
    }
    // A Windows GUI subsystem process started without a console and without redirected handles owns a stream
    // whose handle is invalid; asking the runtime about it would abort the process.
    if (::_get_osfhandle(descriptor) == -1) {
      return false;
    }
    return ::_isatty(descriptor) != 0;
#else
    const auto descriptor = ::fileno(fp);
    return descriptor >= 0 && ::isatty(descriptor) != 0;
#endif
  }

  static bool isColorTerminal() {
#ifdef _WIN32
    return true;
#else
    static const bool result = []() {
      const char* env_colorterm_p = std::getenv("COLORTERM");
      if (env_colorterm_p != nullptr) {
        return true;
      }

      static constexpr std::array<const char*, 16> terms = {
        {
          "ansi", "color", "console", "cygwin", "gnome", "konsole", "kterm", "linux",
          "msys", "putty", "rxvt", "screen", "vt100", "xterm", "alacritty", "vt102"
        }
      };

      const char* env_term_p = std::getenv("TERM");
      if (env_term_p == nullptr) {
        return false;
      }

      return std::any_of(terms.begin(), terms.end(),
        [&](const char* term) { return std::strstr(env_term_p, term) != nullptr; });
    }();

    return result;
#endif
  }
};

template<typename ConsoleMutex>
class AnsiColorStdOutSink : public AnsiColorSink<ConsoleMutex> {
public:
  explicit AnsiColorStdOutSink(ColorMode mode = ColorMode::kAutomatic)
    : AnsiColorSink<ConsoleMutex>(stdout, mode) {
  }
};

template<typename ConsoleMutex>
class AnsiColorStdErrSink : public AnsiColorSink<ConsoleMutex> {
public:
  explicit AnsiColorStdErrSink(ColorMode mode = ColorMode::kAutomatic)
    : AnsiColorSink<ConsoleMutex>(stderr, mode) {
  }
};

using AnsiColorDefaultSink = AnsiColorStdOutSink<ConsoleMutex>;
using AnsiColorErrorSink = AnsiColorStdErrSink<ConsoleMutex>;

} // namespace minilog

#endif // MINILOG_SINKS_ANSI_COLOR_SINK_H
