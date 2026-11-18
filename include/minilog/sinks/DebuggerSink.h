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

#ifndef MINILOG_SINKS_DEBUGGER_SINK_H
#define MINILOG_SINKS_DEBUGGER_SINK_H

#include <minilog/common.h>
#include <minilog/sinks/BaseSink.h>

#include "private/minilog/mutex.h"

#include <cstdio>
#include <mutex>
#include <string>

#if defined(_WIN32)
#include <io.h> // for _fileno and _get_osfhandle
#endif

namespace minilog {

/**
 * Writes every message straight into stdout or stderr and flushes it right away.
 *
 * This is the sink of the debugger consoles of the IDEs: CLion, Visual Studio Code, Xcode, and gdb/lldb in
 * general read the output of the process through a pipe and show it in the debug console. A pipe is block
 * buffered, so the messages of a program that is sitting in a breakpoint stay invisible until the runtime
 * decides to flush - which is what makes this sink flush after every single message. It also writes the
 * messages of a Windows GUI subsystem process: such a process has no console of its own, but it inherits the
 * redirected handles of the IDE that started it.
 *
 * The sink never throws: a debug console that disappears while the process keeps running must not take the
 * process down. Use AnsiColorSink instead when the output is meant for a terminal and should be colored.
 *
 * @tparam Mutex std::mutex for a sink shared by several threads, NoMutex for a sink used by one thread only.
 */
template<typename Mutex>
class DebuggerSink final : public BaseSink<Mutex> {
public:
  enum class Stream : uint8_t {
    kStdOut,
    kStdErr,
  };

  /** The debug console of an IDE shows both streams; stderr is the default because it is never block buffered. */
  explicit DebuggerSink(const Stream stream = Stream::kStdErr) noexcept
    : mStream(stream == Stream::kStdErr ? stderr : stdout) {
  }

  ~DebuggerSink() override = default;

  DebuggerSink(const DebuggerSink&) = delete;
  DebuggerSink& operator=(const DebuggerSink&) = delete;

  [[nodiscard]] Stream stream() const noexcept { return mStream == stderr ? Stream::kStdErr : Stream::kStdOut; }

  /**
   * True when the stream is attached to something.
   *
   * A Windows GUI subsystem process started without a console has no valid stream at all; writing into it would
   * trip the invalid parameter handler of the runtime, so the sink stays silent instead.
   */
  [[nodiscard]] bool isConnected() const noexcept {
    if (mStream == nullptr) {
      return false;
    }

#if defined(_WIN32)
    const auto descriptor = ::_fileno(mStream);
    if (descriptor < 0) {
      return false;
    }
    return ::_get_osfhandle(descriptor) != -1;
#else
    return ::fileno(mStream) != -1;
#endif
  }

protected:
  void onSink(const Message& msg) override {
    // Checked on every message: a console can be attached to the process after the sink was created.
    if (!isConnected()) {
      return;
    }

    std::string formatted;
    this->mFormatter->format(msg, formatted);
    if (formatted.empty()) {
      return;
    }

    // Best effort by design: the debugger may be gone while the process keeps running.
    (void)std::fwrite(formatted.data(), 1, formatted.size(), mStream);
    (void)std::fflush(mStream);
  }

  void onFlush() override {
    if (mStream != nullptr) {
      (void)std::fflush(mStream);
    }
  }

private:
  // Borrowed from the runtime and never closed: closing stdout or stderr of the process is not up to a sink.
  std::FILE* mStream;
};

/** Debugger console sink for loggers shared by several threads. */
using DebuggerSinkMt = DebuggerSink<std::mutex>;

/** Debugger console sink for a logger that lives on a single thread. */
using DebuggerSinkSt = DebuggerSink<NoMutex>;

/** The debugger console sink used by default (stderr, flushed after every message). */
using DebuggerSinkDefault = DebuggerSinkMt;

} // namespace minilog

#endif // MINILOG_SINKS_DEBUGGER_SINK_H
