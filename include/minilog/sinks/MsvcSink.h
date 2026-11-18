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

#ifndef MINILOG_SINKS_MSVC_SINK_H
#define MINILOG_SINKS_MSVC_SINK_H

#include <minilog/common.h>
#include <minilog/sinks/BaseSink.h>

#include "private/minilog/mutex.h"

#include <mutex>
#include <string>

#if defined(_WIN32)

// The Windows entry points are declared here instead of including <windows.h>: a header that is included by
// every file that logs stays cheap, and it cannot clash with the min/max macros of the Windows headers.
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* outputString);
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringW(const wchar_t* outputString);
extern "C" __declspec(dllimport) int __stdcall IsDebuggerPresent();
extern "C" __declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int codePage, unsigned long flags,
  const char* multiByteStr, int multiByteCount, wchar_t* wideCharStr, int wideCharCount);

namespace minilog {

namespace details {

/** The code page of UTF-8, spelled out so that <windows.h> does not have to be included. */
inline constexpr unsigned int kUtf8CodePage = 65001;

} // namespace details

/**
 * Writes the formatted messages into the debug channel of Windows with OutputDebugString.
 *
 * This is the sink of the debugger on Windows:
 *
 *  - Visual Studio shows the messages in the Output window (Debug pane);
 *  - DebugView and any other debugger that listens to the debug channel shows them too;
 *  - the messages reach the user of a Windows GUI subsystem process, which has no console of its own.
 *
 * The messages are only produced while a debugger is attached, unless the check is turned off, so the release
 * build does not pay for the formatting of a log nobody reads.
 *
 * OutputDebugStringA sends the bytes of the message and the debugger decodes them with the ANSI code page of
 * the machine, which turns non-ASCII text into garbage. Set `wideOutput` to send UTF-16 with
 * OutputDebugStringW instead, which is what the Visual Studio output window expects.
 *
 * @tparam Mutex std::mutex for a sink shared by several threads, NoMutex for a sink used by one thread only.
 */
template<typename Mutex>
class MsvcSink : public BaseSink<Mutex> {
public:
  /**
   * @param tag Prefix put in front of every message, empty by default.
   * @param checkDebuggerPresent Skip the messages while no debugger is attached.
   * @param wideOutput Send the messages as UTF-16 with OutputDebugStringW.
   */
  explicit MsvcSink(std::string tag = {}, const bool checkDebuggerPresent = true, const bool wideOutput = false) noexcept
    : mTag(std::move(tag)),
      mCheckDebuggerPresent(checkDebuggerPresent),
      mWideOutput(wideOutput) {
  }

  ~MsvcSink() override = default;

  MsvcSink(const MsvcSink&) = delete;
  MsvcSink& operator=(const MsvcSink&) = delete;

  /** Prefix put in front of every message, empty by default. */
  void setTag(std::string tag) {
    std::lock_guard<Mutex> lock(this->mMutex);
    mTag = std::move(tag);
  }

  [[nodiscard]] std::string tag() const {
    std::lock_guard<Mutex> lock(this->mMutex);
    return mTag;
  }

  void setCheckDebuggerPresent(const bool checkDebuggerPresent) {
    std::lock_guard<Mutex> lock(this->mMutex);
    mCheckDebuggerPresent = checkDebuggerPresent;
  }

  [[nodiscard]] bool checkDebuggerPresent() const {
    std::lock_guard<Mutex> lock(this->mMutex);
    return mCheckDebuggerPresent;
  }

  /** True when the messages are sent as UTF-16, fixed at construction. */
  [[nodiscard]] bool wideOutput() const noexcept { return mWideOutput; }

  /** True when a debugger is attached to the process right now. */
  [[nodiscard]] static bool debuggerPresent() noexcept { return ::IsDebuggerPresent() != 0; }

protected:
  void onSink(const Message& msg) override {
    if (mCheckDebuggerPresent && ::IsDebuggerPresent() == 0) {
      // Nothing listens to the debug output of a process that is not being debugged; skip the formatting.
      return;
    }

    // The tag is the prefix, the formatter appends the message to it.
    std::string formatted = mTag;
    this->mFormatter->format(msg, formatted);

    if (mWideOutput) {
      const auto wide = utf8ToWide(formatted);
      if (!wide.empty()) {
        ::OutputDebugStringW(wide.c_str());
      }
      return;
    }

    // OutputDebugStringA takes a null terminated string; the terminator is added explicitly because the data of
    // the string is only guaranteed to be terminated after the terminator was appended.
    formatted.push_back('\0');
    ::OutputDebugStringA(formatted.c_str());
  }

  void onFlush() override {
    // The debug channel has no buffer of its own, there is nothing to flush.
  }

private:
  std::string mTag;
  bool mCheckDebuggerPresent{ true };
  bool mWideOutput{ false };

  /** UTF-8 to UTF-16 for the wide variant; an empty result means the text could not be converted. */
  static std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
      return {};
    }

    const auto length =
      ::MultiByteToWideChar(details::kUtf8CodePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
      return {};
    }

    std::wstring wide(static_cast<size_t>(length), L'\0');
    if (::MultiByteToWideChar(details::kUtf8CodePage, 0, text.data(), static_cast<int>(text.size()), wide.data(),
          length) != length) {
      return {};
    }
    return wide;
  }
};

/** Debug output sink for loggers shared by several threads. */
using MsvcSinkMt = MsvcSink<std::mutex>;

/** Debug output sink for a logger that lives on a single thread. */
using MsvcSinkSt = MsvcSink<NoMutex>;

/** The debug output sink that matches the naming of the other default sinks. */
using MsvcDefaultSink = MsvcSinkMt;

} // namespace minilog

#endif // defined(_WIN32)

#endif // MINILOG_SINKS_MSVC_SINK_H
