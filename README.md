# Minilog

The minilog is a minimalist, cross-platform logging library. based on C++20.

The library writes through *sinks*: a sink formats one message and sends it somewhere - the console, the debug
output of Windows, a file, a ring buffer in memory. A logger owns one or more sinks and hands every message to
all of them.

## Contents

| Header | Type | Writes into |
| --- | --- | --- |
| `minilog/sinks/AnsiColorSink.h` | `AnsiColorSink` | A `FILE*` stream, colored with ANSI escape codes |
| `minilog/sinks/WinColorSink.h` | `WinColorSink` | A Windows console handle, colored with the console API |
| `minilog/sinks/MsvcSink.h` | `MsvcSink` | The debug channel of Windows (`OutputDebugString`) |
| `minilog/sinks/DebuggerSink.h` | `DebuggerSink` | stdout/stderr, flushed after every message - the debugger console of CLion |
| `minilog/sinks/FileSink.h` | `BasicFileSink` | One file, appended to or truncated |
| `minilog/sinks/RotatingFileSink.h` | `RotatingFileSink` | A file that rotates once it passes a maximum size |
| `minilog/sinks/DailyFileSink.h` | `DailyFileSink` | One file per day |
| `minilog/sinks/AndroidSink.h` | `AndroidSink` | logcat (Android only) |
| `minilog/sinks/OStreamSink.h` | `OStreamSink` | A `std::ostream` - a `std::ostringstream` captures the log of a test |
| `minilog/sinks/RingBufferSink.h` | `RingBufferSink` | The last N formatted messages, kept in memory |
| `minilog/sinks/CallbackSink.h` | `CallbackSink` | A `std::function<void(const Message&)>` of the caller |
| `minilog/sinks/DistSink.h` | `DistSink` | Every sink it holds - the way to combine the outputs above |
| `minilog/sinks/NullSink.h` | `NullSink` | Nowhere, the message is dropped on purpose |

Every sink is a template over the mutex it serialises its calls with:
`...Mt` aliases (`FileSinkMt`, `MsvcSinkMt`, ...) are the multi-threaded ones used by a logger that is shared
between threads, `...St` aliases (`FileSinkSt`, ...) hold no mutex and are for a logger that lives on a single
thread. The plain names (`FileSink`, `RotatingFile`, `DailyFile`) are the multi-threaded ones.

## Setting the sinks up

`minilog/LogSetup.h` builds the sinks of a configuration, installs them on the default logger and applies the
settings to the registry, so a logger created later starts the same way:

```cpp
#include <minilog/log.h>
#include <minilog/LogSetup.h>

int main() {
  minilog::LoggingConfig config;
  config.level = minilog::Level::kDebug;   // the lowest level that is written
  config.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v";

  // The console of the platform, kept as it is by default.
  config.console.enabled = true;
  config.console.colorMode = minilog::ColorMode::kAutomatic;

  // The Output window of Visual Studio, and DebugView. Ignored on every other platform.
  config.debugOutput.enabled = true;
  config.debugOutput.wideOutput = true;    // UTF-16, so non-ASCII text survives

  // A rotating log file under logs/, three generations of 8 MB.
  config.file.enabled = true;
  config.file.directory = "logs";
  config.file.basename = "coral.log";
  config.file.maxFiles = 3;
  config.file.maxFileSize = 8 * 1024 * 1024;
  config.file.flushOnLog = false;          // or true to have every line on disk right away

  minilog::setupLogging(config);

  MINILOG_INFO("the playback started at {}", startTime);
  MINILOG_ERROR("the decoder refused the stream: {}", reason);

  minilog::shutdown();                     // flush and release every logger
}
```

`minilog::ScopedLogging` does the same through RAII: it installs the logging on construction and calls
`minilog::shutdown()` on destruction, which flushes and drops every logger and therefore closes every log file.

```cpp
int main() {
  const minilog::ScopedLogging logging;   // the default configuration: the console only
  MINILOG_INFO("hello");
}                                          // nothing is left open here
```

`config.file` chooses between three kinds of file:

| Setting | File names |
| --- | --- |
| `daily = true` | `logs/coral_2026-01-02.log`, one file per day, `truncate` starts the day fresh |
| `maxFiles > 1` | `logs/coral.log` plus `logs/coral_1.log` ... `logs/coral_<maxFiles-1>.log`, the highest index being the oldest |
| otherwise | `logs/coral.log`, growing without a limit |

## Debugging with an IDE

**Visual Studio (MSVC).** `MsvcSink` sends every message with `OutputDebugString`; the messages appear in the
*Output* window under *Debug*, and in DebugView. Nothing is formatted while no debugger is attached, unless
`checkDebuggerPresent` is turned off, and `wideOutput` sends UTF-16 so that non-ASCII text is not mangled by the
ANSI code page of the machine.

**CLion, Visual Studio Code, Xcode, gdb/lldb.** The debug console of these IDEs is a pipe, and a pipe is block
buffered, so a message written into it stays invisible until the runtime flushes - which is exactly what a
program sitting in a breakpoint never does. `DebuggerSink` writes into stderr and flushes after every single
message, and it also reaches a Windows GUI subsystem process, which has no console of its own but inherits the
redirected handles of the IDE:

```cpp
auto logger = std::make_shared<minilog::Logger>("coral", std::make_shared<minilog::DebuggerSinkMt>());
minilog::setDefaultLogger(logger);
logger->info("visible in the debug console right away, breakpoint or not");
```

Turning the level of the *logger* down with `minilog::setLevel(minilog::Level::kDebug)` is what makes the debug
messages of the project show up; `MINILOG_DEBUG` also has to survive the compile time filter of
`MINILOG_ACTIVE_LEVEL`, which is trace in a Debug build and debug in a release build.

**A log file for a post-mortem.** A file sink is what is left when the process is gone. `flushOnLog` and the
flush level of the logger (`config.flushLevel`, `kError` by default) decide how much of it is already on disk
when the process dies. The files are opened with a share mode that keeps them readable while the process runs,
so they can be followed with `tail -f` or opened in an editor during the session.

**The last messages before something went wrong.** `RingBufferSink` keeps the last messages in memory, so they
can be dumped into a crash report or a dialog:

```cpp
auto tail = std::make_shared<minilog::RingBufferSinkMt>(256);
logger->sinks().push_back(tail);
...
for (const auto& line : tail->lastFormatted()) { report(line); }
```

## RAII

The library never leaks what it opens, and never keeps a sink in a state it cannot write from:

* a sink owns what it opens and releases it in its destructor - the file of a file sink is closed as soon as the
  last `std::shared_ptr` of the sink is gone, which is what lets Windows rename or remove it right away;
* the pattern formatter of a sink is owned by `std::unique_ptr`, a `setFormatter(nullptr)` is refused instead of
  leaving a sink that cannot format anything;
* `FileHelper`, the file owner behind the file sinks, closes the previous stream before it opens the next one, so
  a failing open can neither leak the old handle nor leave a half opened sink;
* a rotation closes the file before the files are renamed (Windows refuses to move an open file) and always
  opens a file again - a failed rotation keeps logging into the current file and reports the error, it never
  leaves the sink without a file;
* the streams a sink borrows (`stdout`, `stderr`, a `FILE*` of the caller, a console handle, a `std::ostream`)
  are flushed but never closed by the sink;
* a sink that throws - a full disk, a log directory that was removed while the process runs - is reported
  through the error handler of the logger (`Logger::setErrorHandler`) and the remaining sinks of the logger still
  get the message;
* `DistSink` owns its sinks through `std::shared_ptr`, refuses to contain itself and hands its level and its
  pattern down to them;
* `minilog::shutdown()` flushes and drops every logger, and the default logger is re-created on demand, so a log
  call after it never dereferences a null logger.

## Usage examples

`demo/` contains four standalone programs that show the library in use:

| Example | Shows |
| --- | --- |
| `minilog_demo_quickstart` | `ScopedLogging`, a console-only configuration, `%+` and the log macros. |
| `minilog_demo_file_logging` | A console sink and a rotating file sink, a pattern and a level per sink, and `flushLevel`. |
| `minilog_demo_custom_sinks` | A hand-built `Logger` with `DistSink`: an `OStreamSink`, a `RingBufferSink` and a `CallbackSink`. |
| `minilog_demo_backtrace` | `enableBacktrace()` and `dumpBacktrace()` for the messages below the logger level. |

They are built when `MINILOG_BUILD_DEMOS` is on, which is the default when minilog is the top-level project:

```sh
cmake -S . -B build -DMINILOG_BUILD_DEMOS=ON
cmake --build build --target minilog_demo_quickstart
./build/demo/minilog_demo_quickstart
```

See `demo/README.md` for the other targets and for the notes on where the examples write their files.

## Tests

`tests/tests_MiniLogSinks.cpp` covers the sinks, the RAII rules above, `setupLogging()` and the registry.
`tests/tests_MiniLogCore.cpp` covers the pattern formatter, the logger, the backtrace, the MDC,
`Message`/`MessageBuffer` and the file and path helpers of `Utils`.

Both are standalone executables registered with CTest. Each one writes its log files below the current
directory; CTest pins that directory to the build directory of the test.

```sh
cmake -S . -B build -DMINILOG_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests are built by default when minilog is the top-level project; `MINILOG_BUILD_TESTS=OFF` turns them
off.
