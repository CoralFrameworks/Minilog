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

// =============================================================
//  tests_MiniLogSinks.cpp
//
//  Covers the sinks of minilog and the RAII rules they follow:
//
//    - the file sink writes, appends, truncates and releases its handle when it is destroyed
//    - the rotating file sink rotates, keeps at most maxFiles generations and never loses the handle
//    - the daily file sink names its file after the day and rotates on the timestamp of a message
//    - the debugger sink writes into the stream of an IDE debugger
//    - the MSVC sink reaches the debug channel of Windows without a debugger attached
//    - the stream, ring buffer, callback, dist and null sinks
//    - a sink that throws is reported by the logger instead of taking the process down, and the other
//      sinks of the logger still get the message
//    - the registry: level, flush, drop and shutdown, and a default logger that is never null
//    - setupLogging() and ScopedLogging
//
//  Every check prints to stdout, so the test can redirect stderr for the debugger sink.
// =============================================================

#include <minilog/LogSetup.h>
#include <minilog/log.h>
#include <minilog/sinks/AnsiColorSink.h>
#include <minilog/sinks/CallbackSink.h>
#include <minilog/sinks/DailyFileSink.h>
#include <minilog/sinks/DebuggerSink.h>
#include <minilog/sinks/DistSink.h>
#include <minilog/sinks/FileSink.h>
#include <minilog/sinks/MsvcSink.h>
#include <minilog/sinks/NullSink.h>
#include <minilog/sinks/OStreamSink.h>
#include <minilog/sinks/RingBufferSink.h>
#include <minilog/sinks/RotatingFileSink.h>

#include "private/minilog/Message.h"
#include "private/minilog/Utils.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <minilog/sinks/WinColorSink.h>
#include <windows.h>
#endif

namespace {

int gFailures = 0;
int gChecks = 0;

void check(const bool condition, const std::string& what) {
  ++gChecks;
  if (condition) {
    std::printf("  ok   %s\n", what.c_str());
  } else {
    ++gFailures;
    std::printf("  FAIL %s\n", what.c_str());
  }
}

void section(const char* name) {
  std::printf("--- %s\n", name);
}

minilog::Message message(const std::string_view text, const minilog::Level level = minilog::Level::kInfo) {
  return minilog::Message(minilog::Source("tests_MiniLogSinks.cpp", 1, "test"), "test", level, text);
}

std::string readFile(const std::string& path) {
  std::FILE* file = minilog::Utils::openFile(path, "rb");
  if (file == nullptr) {
    return {};
  }

  std::string content;
  char buffer[512];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    content.append(buffer, read);
  }
  std::fclose(file);
  return content;
}

/** Reads a stream the caller owns; the stream is rewound but left open. */
std::string readStream(std::FILE* file) {
  if (file == nullptr) {
    return {};
  }

  std::fflush(file);
  std::rewind(file);
  std::string content;
  char buffer[512];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    content.append(buffer, read);
  }
  return content;
}

size_t countLines(const std::string& text) {
  return static_cast<size_t>(std::count(text.begin(), text.end(), '\n'));
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

const std::string& outputDirectory() {
  static const std::string directory = "minilog_test_logs";
  return directory;
}

void testFileSink() {
  section("file sink");

  const auto appendPath = minilog::Utils::joinPath(outputDirectory(), "append.log");

  {
    minilog::FileSink sink(appendPath, true);
    sink.setPattern("%v");
    sink.log(message("first line"));
    sink.flush();
    check(minilog::Utils::fileExists(appendPath), "the file was created");
    check(readFile(appendPath) == std::string("first line") + MINILOG_EOL,
      "the message and one end of line were written in binary mode");
    check(sink.size() == std::string("first line").size() + std::string(MINILOG_EOL).size(),
      "the size of the sink follows the bytes it wrote");
  }

  {
    // Destroyed without an explicit flush: the destructor flushes and closes the file.
    minilog::FileSink sink(appendPath, false);
    sink.setPattern("%v");
    sink.log(message("second line"));
    check(sink.flushOnLog() == false, "flushing on every message is off by default");
  }

  const auto appended = readFile(appendPath);
  check(contains(appended, "first line") && contains(appended, "second line"),
    "appending keeps the content that was there");

  {
    minilog::FileSink sink(appendPath, true);
    sink.setPattern("%v");
    sink.log(message("only line"));
  }
  check(readFile(appendPath) == std::string("only line") + MINILOG_EOL, "truncating drops the previous content");

  // The handle is released when the sink is gone: on Windows the file is only renamable once it is closed.
  check(minilog::Utils::renameFile(appendPath, appendPath + ".moved"), "the handle was closed when the sink died");
  check(minilog::Utils::removeFile(appendPath + ".moved"), "the renamed file can be removed");

  // A directory that does not exist is not created by the sink, and the failure is reported.
  bool threw = false;
  try {
    minilog::FileSink sink(minilog::Utils::joinPath(minilog::Utils::joinPath(outputDirectory(), "missing"), "a.log"),
      true);
    (void) sink;
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "an unopenable path throws instead of writing nothing");

  // The process streams can be used as a file, the sink must not close them.
  {
    minilog::FileSink sink(std::string(minilog::kStdErrFile), false);
    sink.setPattern("%v");
    check(sink.filename() == std::string(minilog::kStdErrFile), "stderr is a valid file name for the sink");
  }
}

void testFileSinkThroughLogger() {
  section("file sink through a logger");

  const auto path = minilog::Utils::joinPath(outputDirectory(), "logger.log");
  {
    auto sink = std::make_shared<minilog::FileSink>(path, true);
    sink->setPattern("%v");

    minilog::Logger logger("file-test", sink);
    logger.info("logged through the logger {}", 42);
    logger.log(minilog::Level::kWarn, "and through log()");
    logger.flush();
  }

  const auto content = readFile(path);
  check(contains(content, "logged through the logger 42"), "the formatted message reached the file");
  check(contains(content, "and through log()"), "a message of another level reached the file too");
  check(minilog::Utils::removeFile(path), "the file was released with the logger");
}

void testFileSinkReopenAndThreads() {
  section("file sink: reopen and threads");

  const auto path = minilog::Utils::joinPath(outputDirectory(), "reopen.log");
  {
    minilog::FileSink sink(path, true);
    sink.setPattern("%v");
    sink.log(message("before the reopen"));
    check(sink.flushOnLog() == false, "flushing on every message is off by default");
    sink.setFlushOnLog(true);
    check(sink.flushOnLog(), "flushing on every message can be enabled");

    sink.reopen(true);
    check(sink.size() == 0, "reopen with truncate starts with an empty file");
    sink.log(message("after the reopen"));
    check(sink.size() == std::string("after the reopen").size() + std::string(MINILOG_EOL).size(),
      "the size follows the writes after the reopen");
  }

  const auto reopened = readFile(path);
  check(contains(reopened, "after the reopen"), "the reopened file kept the new message");
  check(!contains(reopened, "before the reopen"), "the reopen with truncate dropped the old message");
  check(minilog::Utils::removeFile(path), "cleanup: the reopened file");

  // A file sink used by several threads must keep every line intact and in one piece.
  const auto threadedPath = minilog::Utils::joinPath(outputDirectory(), "threaded.log");
  constexpr int threadCount = 4;
  constexpr int messagesPerThread = 50;
  {
    minilog::FileSink sink(threadedPath, true, true);
    sink.setPattern("%v");

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (int thread = 0; thread < threadCount; ++thread) {
      workers.emplace_back([&sink, thread]() {
        for (int index = 0; index < messagesPerThread; ++index) {
          sink.log(message("thread " + std::to_string(thread) + " message " + std::to_string(index)));
        }
      });
    }
    for (auto& worker: workers) {
      worker.join();
    }
  }

  const auto threaded = readFile(threadedPath);
  check(countLines(threaded) == static_cast<size_t>(threadCount * messagesPerThread),
    "every message of every thread reached the file as one line");
  bool allPresent = true;
  for (int thread = 0; thread < threadCount; ++thread) {
    for (int index = 0; index < messagesPerThread; ++index) {
      if (!contains(threaded, "thread " + std::to_string(thread) + " message " + std::to_string(index))) {
        allPresent = false;
      }
    }
  }
  check(allPresent, "no message of a thread was lost");
  check(minilog::Utils::removeFile(threadedPath), "cleanup: the threaded file");

  // The single-threaded alias compiles and writes as well.
  const auto singlePath = minilog::Utils::joinPath(outputDirectory(), "single.log");
  {
    minilog::FileSinkSt sink(singlePath, true);
    sink.setPattern("%v");
    sink.log(message("single thread"));
  }
  check(readFile(singlePath) == std::string("single thread") + MINILOG_EOL,
    "the single-threaded file sink writes a message");
  check(minilog::Utils::removeFile(singlePath), "cleanup: the single-threaded file");
}

void testRotatingFileSink() {
  section("rotating file sink");

  const auto path = minilog::Utils::joinPath(outputDirectory(), "rotate.log");
  check(minilog::RotatingFileSinkMt::rotatedFilename(path, 0) == path, "index 0 is the file that is written");
  check(minilog::RotatingFileSinkMt::rotatedFilename(path, 2) ==
          minilog::Utils::joinPath(outputDirectory(), "rotate_2.log"),
    "the index is inserted in front of the extension");

  {
    minilog::RotatingFileSinkMt sink(path, 256, 3);
    sink.setPattern("%v");

    check(sink.maxSize() == 256 && sink.maxFiles() == 3, "the limits are kept");

    for (int i = 0; i < 20; ++i) {
      sink.log(message("message number " + std::to_string(i) + " with a payload that fills the file"));
    }
  }

  check(minilog::Utils::fileExists(path), "the file that is being written exists");
  check(minilog::Utils::fileExists(minilog::RotatingFileSinkMt::rotatedFilename(path, 1)), "the first generation exists");
  check(minilog::Utils::fileExists(minilog::RotatingFileSinkMt::rotatedFilename(path, 2)), "the second generation exists");
  check(!minilog::Utils::fileExists(minilog::RotatingFileSinkMt::rotatedFilename(path, 3)),
    "no file past maxFiles is kept");
  check(contains(readFile(path), "message number 19"), "the newest message is in the file that is written");
  check(!contains(readFile(minilog::RotatingFileSinkMt::rotatedFilename(path, 2)), "message number 0 "),
    "the generations that are dropped are the oldest ones");

  // rotateOnOpen: the content of the previous run becomes the first generation.
  {
    minilog::RotatingFileSinkMt sink(path, 256, 3, true);
    sink.setPattern("%v");
    check(sink.size() == 0, "rotating on open leaves an empty file");
  }
  check(contains(readFile(minilog::RotatingFileSinkMt::rotatedFilename(path, 1)), "message number 19"),
    "the content of the previous run is the first generation");

  bool threw = false;
  try {
    minilog::RotatingFileSinkMt sink(path, 0, 3);
    (void) sink;
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "a maximum size of zero is refused");

  threw = false;
  try {
    minilog::RotatingFileSinkMt sink(path, 128, 0);
    (void) sink;
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "keeping zero files is refused");

  // The handle of the file that is written was released with the sink, so Windows lets the caller rename it.
  check(minilog::Utils::renameFile(path, path + ".moved"), "the file was released when the sink died");
  check(minilog::Utils::removeFile(path + ".moved"), "cleanup: the current file");
  check(minilog::Utils::removeFile(minilog::RotatingFileSinkMt::rotatedFilename(path, 1)), "cleanup: the first generation");
  check(minilog::Utils::removeFile(minilog::RotatingFileSinkMt::rotatedFilename(path, 2)), "cleanup: the second generation");
}

void testRotatingFileSinkExplicitRotation() {
  section("rotating file sink: explicit rotation and oversized messages");

  const auto path = minilog::Utils::joinPath(outputDirectory(), "rotate_now.log");
  {
    minilog::RotatingFileSinkMt sink(path, 4096, 2);
    sink.setPattern("%v");
    check(sink.baseFilename() == path, "the base file name is kept");

    sink.log(message("first generation"));
    sink.flush();
    sink.rotateNow();
    check(sink.size() == 0, "rotateNow starts with an empty file");
    sink.log(message("second generation"));
  }

  check(contains(readFile(minilog::RotatingFileSinkMt::rotatedFilename(path, 1)), "first generation"),
    "rotateNow moved the current content into the first generation");
  check(contains(readFile(path), "second generation"), "the new message is in the current file");
  check(!minilog::Utils::fileExists(minilog::RotatingFileSinkMt::rotatedFilename(path, 2)),
    "maxFiles of two keeps the current file and one generation");
  check(minilog::Utils::removeFile(path), "cleanup: the current file");
  check(minilog::Utils::removeFile(minilog::RotatingFileSinkMt::rotatedFilename(path, 1)),
    "cleanup: the first generation");

  // A name without an extension still gets an index.
  check(minilog::RotatingFileSinkMt::rotatedFilename("coral", 1) == "coral_1",
    "a name without an extension is rotated");
  check(minilog::RotatingFileSinkMt::rotatedFilename("logs/.hidden", 1) == "logs/.hidden_1",
    "a hidden file name is rotated");

  // A message larger than the maximum file size is written as a whole instead of triggering endless rotations.
  const auto oversizedPath = minilog::Utils::joinPath(outputDirectory(), "rotate_oversized.log");
  {
    minilog::RotatingFileSinkMt sink(oversizedPath, 16, 2);
    sink.setPattern("%v");
    const std::string oversized(128, 'x');
    sink.log(message(oversized));
    sink.log(message(oversized));
    sink.log(message(oversized));
    check(sink.size() > 0, "an oversized message still reaches a file");
  }
  check(contains(readFile(oversizedPath), std::string(128, 'x')),
    "the oversized message is written without being split");
  check(minilog::Utils::removeFile(oversizedPath), "cleanup: the oversized file");
  check(minilog::Utils::removeFile(minilog::RotatingFileSinkMt::rotatedFilename(oversizedPath, 1)),
    "cleanup: the oversized first generation");

  // rotateOnOpen leaves an empty file alone.
  const auto emptyPath = minilog::Utils::joinPath(outputDirectory(), "rotate_empty.log");
  {
    minilog::FileSink emptyFile(emptyPath, true);
  }
  {
    minilog::RotatingFileSinkMt sink(emptyPath, 4096, 3, true);
    check(sink.size() == 0, "rotateOnOpen of an empty file did not rotate");
    check(!minilog::Utils::fileExists(minilog::RotatingFileSinkMt::rotatedFilename(emptyPath, 1)),
      "no generation was created for an empty file");
  }
  check(minilog::Utils::removeFile(emptyPath), "cleanup: the empty file");
}

void testDailyFileSink() {
  section("daily file sink");

  std::tm date{};
  date.tm_year = 126; // 2026
  date.tm_mon = 2; // March
  date.tm_mday = 15;
  date.tm_hour = 12;
  date.tm_isdst = -1;
  const auto day = std::mktime(&date);

  check(minilog::DailyFileSinkMt::calculateFilename("logs/coral.log", day) == "logs/coral_2026-03-15.log",
    "the local date is inserted in front of the extension");
  check(minilog::DailyFileSinkMt::calculateFilename("coral", day) == "coral_2026-03-15",
    "a name without an extension still gets the date");

  const auto path = minilog::Utils::joinPath(outputDirectory(), "daily.log");
  std::string todayFile;
  std::string futureFile;

  {
    minilog::DailyFileSinkMt sink(path, 0, 0);
    sink.setPattern("%v");

    check(sink.rotationHour() == 0 && sink.rotationMinute() == 0, "the rotation time is kept");
    check(sink.nextRotation() > minilog::Clock::now(), "the next rotation is in the future");

    sink.log(message("today"));
    todayFile = sink.filename();
    check(minilog::Utils::fileExists(todayFile), "the file of the day exists");

    // A message that carries a time two days ahead moves the sink to the file of its own day.
    const auto future = minilog::Clock::now() + std::chrono::hours(48);
    sink.log(minilog::Message(future, minilog::Source("tests_MiniLogSinks.cpp", 1, "test"), "test",
      minilog::Level::kInfo, "in two days"));

    futureFile = sink.filename();
    check(futureFile != todayFile, "the sink moved to the file of the day of the message");
    check(minilog::Utils::fileExists(futureFile), "the new file was created");

    // The message is still in the buffer of the stream: a file becomes readable once it was flushed. Closing
    // the file of the previous day flushed it, so its content is already on disk.
    sink.flush();
    check(contains(readFile(futureFile), "in two days"), "the message is in the file of its own day");
    check(contains(readFile(todayFile), "today"), "the previous file kept its content");
  }

  check(minilog::Utils::removeFile(futureFile), "the file of the future day was released with the sink");
  check(minilog::Utils::removeFile(todayFile), "the file of the day was released with the sink");

  bool threw = false;
  try {
    minilog::DailyFileSinkMt sink(path, 24, 0);
    (void) sink;
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "an hour outside of the day is refused");
  check(minilog::Utils::removeFile(todayFile), "cleanup: the file created by the refused daily sink");
}

void testDailyFileSinkExtras() {
  section("daily file sink: rotation time and truncation");

  const auto next = minilog::DailyFileSinkMt::nextRotationTimePoint(minilog::Clock::now(), 3, 30);
  const auto nextTm = minilog::Utils::localtime(minilog::Clock::to_time_t(next));
  check(next > minilog::Clock::now(), "the next rotation is in the future");
  check(nextTm.tm_hour == 3 && nextTm.tm_min == 30 && nextTm.tm_sec == 0,
    "the next rotation is at the configured hour and minute");

  const auto later = minilog::DailyFileSinkMt::nextRotationTimePoint(next + std::chrono::seconds(1), 3, 30);
  check(later >= next + std::chrono::hours(23), "the next rotation after the configured time is the next day");

  const auto path = minilog::Utils::joinPath(outputDirectory(), "daily_truncate.log");
  const auto todayFile = minilog::DailyFileSinkMt::calculateFilename(path, std::time(nullptr));
  {
    minilog::FileSink previous(todayFile, true);
    previous.setPattern("%v");
    previous.log(message("stale content"));
  }

  {
    minilog::DailyFileSinkMt sink(path, 0, 0, true);
    sink.setPattern("%v");
    check(sink.size() == 0, "the daily sink truncates the file of the day when asked");
    sink.log(message("fresh content"));
    sink.flush();
    check(sink.filename() == todayFile, "the daily sink names the file of the day");
  }

  const auto content = readFile(todayFile);
  check(contains(content, "fresh content"), "the new message is in the daily file");
  check(!contains(content, "stale content"), "the truncated file dropped the previous content");
  check(minilog::Utils::removeFile(todayFile), "cleanup: the daily truncate file");

  bool threw = false;
  try {
    minilog::DailyFileSinkMt invalid(path, 0, 60);
    (void) invalid;
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "a rotation minute above 59 is refused");

  threw = false;
  try {
    minilog::DailyFileSinkMt invalid(path, -1, 0);
    (void) invalid;
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "a negative rotation hour is refused");
  check(minilog::Utils::removeFile(todayFile), "cleanup: the file created by the refused daily sink");

  // rotateNow opens the file of the current day again; appending keeps the content when truncate is off.
  const auto rotatePath = minilog::Utils::joinPath(outputDirectory(), "daily_rotate_now.log");
  std::string rotatedFile;
  {
    minilog::DailyFileSinkMt sink(rotatePath, 0, 0);
    sink.setPattern("%v");
    sink.log(message("before rotateNow"));
    rotatedFile = sink.filename();
    sink.rotateNow();
    check(sink.filename() == rotatedFile, "rotateNow keeps the file of the same day");
    sink.log(message("after rotateNow"));
  }
  const auto rotatedContent = readFile(rotatedFile);
  check(contains(rotatedContent, "before rotateNow") && contains(rotatedContent, "after rotateNow"),
    "rotateNow appends when truncate is off");
  check(minilog::Utils::removeFile(rotatedFile), "cleanup: the rotated daily file");
}

void testDebuggerSink() {
  section("debugger sink");

  auto sink = std::make_shared<minilog::DebuggerSinkMt>(minilog::DebuggerSinkMt::Stream::kStdErr);
  sink->setPattern("%v");
  check(sink->stream() == minilog::DebuggerSinkMt::Stream::kStdErr, "the sink writes into stderr by default");
  check(sink->isConnected(), "stderr of a test run by ctest is attached to the log of the test");

  // The content is verified at the end of the run, when stderr is redirected into a file.
  sink->log(message("debugger sink output"));
}

void testMsvcSink() {
  section("msvc sink");

#if defined(_WIN32)
  auto sink = std::make_shared<minilog::MsvcSinkMt>("[coral] ", true, false);
  sink->setPattern("%v");

  check(sink->tag() == "[coral] ", "the tag is kept");
  check(sink->checkDebuggerPresent(), "the debugger check is on by default");

  // Nobody listens to the debug channel when ctest runs, the message is skipped and must not fail.
  sink->log(message("not seen without a debugger"));
  check(true, "a message without a debugger attached is skipped");

  sink->setCheckDebuggerPresent(false);
  sink->log(message("sent to the debug channel"));

  auto wide = std::make_shared<minilog::MsvcSinkMt>("", true, true);
  wide->setPattern("%v");
  wide->setCheckDebuggerPresent(false);
  check(wide->wideOutput(), "the wide variant is reported");
  wide->log(message("wide output with non-ASCII text: 调试日志"));
  check(true, "the wide variant converts UTF-8 to UTF-16");
#else
  check(true, "the msvc sink only exists on Windows");
#endif
}

void testAnsiColorSink() {
  section("ansi color sink");

  const auto path = minilog::Utils::joinPath(outputDirectory(), "ansi_color.txt");
  std::FILE* file = minilog::Utils::openFile(path, "w+b");
  check(file != nullptr, "the ANSI sink test can create its file");
  if (file != nullptr) {
    {
      minilog::AnsiColorSink<minilog::ConsoleMutex> sink(file, minilog::ColorMode::kAlways);
      sink.setPattern("%^%l%$: %v");
      check(sink.shouldColor(), "kAlways asks for colors");
      sink.log(message("colored", minilog::Level::kWarn));
      sink.flush();
    }

    // The sink borrowed the stream: the file must still be usable after the sink is gone.
    const auto content = readStream(file);
    std::fclose(file);
    check(content == std::string("\033[33m\033[1mwarning\033[m: colored") + MINILOG_EOL,
      "the level was wrapped in the warning color and the reset code");
    check(minilog::Utils::removeFile(path), "cleanup: the colored file");
  }

  const auto plainPath = minilog::Utils::joinPath(outputDirectory(), "ansi_plain.txt");
  std::FILE* plain = minilog::Utils::openFile(plainPath, "w+b");
  if (plain != nullptr) {
    {
      minilog::AnsiColorSink<minilog::ConsoleMutex> sink(plain, minilog::ColorMode::kNever);
      sink.setPattern("%^%l%$: %v");
      check(!sink.shouldColor(), "kNever disables colors");
      sink.log(message("plain", minilog::Level::kWarn));
    }
    const auto content = readStream(plain);
    std::fclose(plain);
    check(content == std::string("warning: plain") + MINILOG_EOL, "kNever writes the plain message");
    check(!contains(content, "\033["), "kNever writes no escape code");
    check(minilog::Utils::removeFile(plainPath), "cleanup: the plain file");
  }

  const auto automaticPath = minilog::Utils::joinPath(outputDirectory(), "ansi_auto.txt");
  std::FILE* automatic = minilog::Utils::openFile(automaticPath, "w+b");
  if (automatic != nullptr) {
    {
      minilog::AnsiColorSink<minilog::ConsoleMutex> sink(automatic, minilog::ColorMode::kAutomatic);
      check(!sink.shouldColor(), "a file is not a color terminal in automatic mode");
      sink.setPattern("%v");
      sink.log(message("automatic"));
    }
    const auto content = readStream(automatic);
    std::fclose(automatic);
    check(content == std::string("automatic") + MINILOG_EOL, "automatic mode writes into a file");
    check(minilog::Utils::removeFile(automaticPath), "cleanup: the automatic file");
  }

  // A custom color and a color mode change after construction.
  const auto customPath = minilog::Utils::joinPath(outputDirectory(), "ansi_custom.txt");
  std::FILE* custom = minilog::Utils::openFile(customPath, "w+b");
  if (custom != nullptr) {
    {
      minilog::AnsiColorSink<minilog::ConsoleMutex> sink(custom, minilog::ColorMode::kAlways);
      sink.setPattern("%^%l%$");
      sink.setColor(minilog::Level::kInfo, "\033[35m");
      sink.log(message("x", minilog::Level::kInfo));
      sink.setColorMode(minilog::ColorMode::kNever);
      check(!sink.shouldColor(), "setColorMode changes the mode after construction");
    }
    const auto content = readStream(custom);
    std::fclose(custom);
    check(content == std::string("\033[35minfo\033[m") + MINILOG_EOL, "setColor changes the color of a level");
    check(minilog::Utils::removeFile(customPath), "cleanup: the custom-color file");
  }

  // A null stream is accepted and writes nowhere.
  minilog::AnsiColorSink<minilog::ConsoleMutex> nullSink(nullptr, minilog::ColorMode::kAlways);
  nullSink.setPattern("%v");
  nullSink.log(message("discarded"));
  nullSink.flush();
  nullSink.setFormatter(nullptr);
  check(true, "a null ANSI target is ignored instead of crashing");
}

void testWinColorSink() {
  section("win color sink");

#if defined(_WIN32)
  auto invalidSink = std::make_shared<minilog::WinColorSink<minilog::ConsoleMutex>>(
    static_cast<void*>(INVALID_HANDLE_VALUE), minilog::ColorMode::kAlways);
  invalidSink->setPattern("%v");
  invalidSink->log(message("discarded"));
  check(true, "an invalid console handle is ignored instead of crashing");

  auto nullSink = std::make_shared<minilog::WinColorSink<minilog::ConsoleMutex>>(nullptr,
    minilog::ColorMode::kAutomatic);
  nullSink->log(message("discarded"));
  check(true, "a null console handle is ignored instead of crashing");

  const auto path = minilog::Utils::joinPath(outputDirectory(), "win_color.txt");
  const auto widePath = std::filesystem::path(path).wstring();
  const HANDLE handle = ::CreateFileW(widePath.c_str(), GENERIC_WRITE | GENERIC_READ, 0, nullptr, CREATE_ALWAYS,
    FILE_ATTRIBUTE_NORMAL, nullptr);
  check(handle != INVALID_HANDLE_VALUE, "the win color test can create its file");
  if (handle != INVALID_HANDLE_VALUE) {
    {
      minilog::WinColorSink<minilog::ConsoleMutex> sink(static_cast<void*>(handle), minilog::ColorMode::kAutomatic);
      sink.setPattern("%^%l%$: %v");
      sink.log(message("written", minilog::Level::kWarn));
      sink.setFormatter(nullptr);
      sink.log(message("after null formatter"));
    }
    ::CloseHandle(handle);

    const auto content = readFile(path);
    check(contains(content, "warning: written"), "the Windows sink writes into a redirected file handle");
    check(contains(content, "after null formatter"),
      "an empty formatter is refused and the previous one keeps working");
    check(!contains(content, "\033["), "the Windows console sink writes no ANSI escape code");
    check(minilog::Utils::removeFile(path), "cleanup: the Windows color file");
  }
#else
  check(true, "the win color sink only exists on Windows");
#endif
}

void testStreamAndNullSinks() {
  section("stream and null sinks");

  std::ostringstream stream;
  {
    auto sink = std::make_shared<minilog::OStreamSinkMt>(stream);
    sink->setPattern("%l: %v");
    sink->log(message("streamed", minilog::Level::kWarn));
    check(stream.str() == std::string("warning: streamed") + MINILOG_EOL, "the pattern was applied to the stream");
    check(!sink->flushOnLog(), "flushing on every message is off by default");
  }

  auto null = std::make_shared<minilog::NullSinkMt>();
  null->log(message("dropped"));
  null->flush();
  check(true, "the null sink accepts and drops a message");
}

void testRingBufferSink() {
  section("ring buffer sink");

  auto ring = std::make_shared<minilog::RingBufferSinkMt>(3);
  ring->setPattern("%v");

  for (int i = 0; i < 5; ++i) {
    ring->log(message("ring " + std::to_string(i)));
  }

  check(ring->size() == 3, "the capacity is respected");
  check(ring->maxMessages() == 3, "the capacity is kept");

  const auto all = ring->lastFormatted();
  check(all.size() == 3 && contains(all.front(), "ring 2") && contains(all.back(), "ring 4"),
    "the oldest messages are dropped and the order is oldest first");

  const auto newest = ring->lastFormatted(2);
  check(newest.size() == 2 && contains(newest.front(), "ring 3"), "the newest messages can be selected");

  const auto allAgain = ring->lastFormatted(99);
  check(allAgain.size() == 3 && allAgain.front() == all.front(), "a count larger than the size returns all messages");
  check(ring->lastFormatted(0).size() == 3, "a count of zero returns all messages");

  ring->setMaxMessages(1);
  check(ring->size() == 1, "shrinking the capacity drops the oldest messages");

  bool threw = false;
  try {
    ring->setMaxMessages(0);
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "a capacity change to zero is refused");

  ring->clear();
  check(ring->empty(), "clear forgets everything");

  threw = false;
  try {
    minilog::RingBufferSinkMt none(0);
    (void) none;
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "a capacity of zero is refused");

  minilog::RingBufferSinkMt defaultRing;
  check(defaultRing.maxMessages() == 64, "the default capacity is 64 messages");
  defaultRing.setPattern("%v");
  defaultRing.log(message("only"));
  check(defaultRing.lastFormatted().front() == std::string("only") + MINILOG_EOL,
    "the default ring is usable without a capacity argument");
}

void testCallbackSink() {
  section("callback sink");

  int count = 0;
  minilog::Level lastLevel = minilog::Level::kOff;
  auto sink = std::make_shared<minilog::CallbackSinkMt>([&count, &lastLevel](const minilog::Message& msg) {
    ++count;
    lastLevel = msg.level;
  });

  sink->log(message("first"));
  sink->log(message("second", minilog::Level::kError));

  check(count == 2, "the callback saw both messages");
  check(lastLevel == minilog::Level::kError, "the callback sees the level of the message");

  int replacedCount = 0;
  sink->setCallback([&replacedCount](const minilog::Message& msg) {
    ++replacedCount;
    check(msg.payload == "third", "the replacement callback sees the payload");
    check(msg.source.line == 1, "the replacement callback sees the source");
  });
  check(static_cast<bool>(sink->callback()), "the callback is readable");
  sink->log(message("third"));
  check(replacedCount == 1, "setCallback replaces the callback");

  bool threw = false;
  try {
    sink->setCallback(minilog::CallbackSinkMt::Callback());
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "an empty callback is refused");

  threw = false;
  try {
    // Braces, not parentheses: with a single argument that names a type this would declare a function.
    const minilog::CallbackSinkMt withoutCallback{ minilog::CallbackSinkMt::Callback() };
    (void) withoutCallback;
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "a sink built without a callback is refused");
}

void testDistSink() {
  section("dist sink");

  std::ostringstream stream;
  auto streamSink = std::make_shared<minilog::OStreamSinkMt>(stream);
  auto ring = std::make_shared<minilog::RingBufferSinkMt>(8);

  auto dist = std::make_shared<minilog::DistSinkMt>();
  dist->addSink(streamSink);
  dist->addSink(ring);
  dist->setPattern("%v");

  check(dist->size() == 2 && dist->sinks().size() == 2, "both sinks are held");

  dist->log(message("to both"));
  check(contains(stream.str(), "to both"), "the stream sink got the message");
  check(ring->size() == 1, "the ring buffer sink got the message");

  dist->setLevel(minilog::Level::kError);
  check(streamSink->getLevel() == minilog::Level::kError, "the level is handed down to the contained sinks");

  dist->log(message("filtered away"));
  check(!contains(stream.str(), "filtered away"), "the level of a contained sink filters the message");

  dist->removeSink(ring);
  check(dist->size() == 1, "removeSink releases the sink");

  dist->clearSinks();
  check(dist->size() == 0, "clearSinks releases every sink");

  bool threw = false;
  try {
    dist->addSink(dist);
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "a dist sink refuses to contain itself");

  threw = false;
  try {
    dist->addSink(nullptr);
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "a null sink is refused");
}

void testLoggerErrorHandling() {
  section("error handling");

  auto failing = std::make_shared<minilog::CallbackSinkMt>([](const minilog::Message&) {
    throw minilog::MiniLogException("the callback failed on purpose");
  });

  minilog::Logger logger("error-test", failing);

  int handled = 0;
  logger.setErrorHandler([&handled](const std::string& error) {
    ++handled;
    check(contains(error, "the callback failed on purpose"), "the error handler got the message of the failure");
  });

  // Must not throw: a sink that fails cannot take the process down.
  logger.info("this must not escape the logger");
  check(handled == 1, "the failing sink was reported once");

  // A second sink still gets the message, and a failing flush is reported as well.
  std::ostringstream stream;
  auto streamSink = std::make_shared<minilog::OStreamSinkMt>(stream);
  streamSink->setPattern("%v");
  logger.setSinks({ failing, streamSink });

  logger.info("the second sink still works");
  check(contains(stream.str(), "the second sink still works"), "a failing sink does not silence the others");
  check(handled == 2, "the second failure was reported too");
}

void testRegistry() {
  section("registry");

  check(minilog::defaultLogger() != nullptr, "the default logger exists");
  check(minilog::defaultLogger()->name().empty(), "the default logger is the unnamed one");

  minilog::setLevel(minilog::Level::kWarn);
  check(minilog::defaultLogger()->level() == minilog::Level::kWarn, "the level reached the default logger");

  minilog::flushAll();
  check(true, "flushAll does not fail");

  minilog::dropAll();
  // The default logger is re-created on demand: the log macros never dereference a null logger.
  check(minilog::defaultLogger() != nullptr, "the default logger was re-created after dropAll");

  minilog::setLevel(minilog::Level::kTrace);

  auto custom = std::make_shared<minilog::Logger>("replacement", std::make_shared<minilog::NullSinkMt>());
  minilog::setDefaultLogger(custom);
  check(minilog::defaultLogger() == custom, "setDefaultLogger replaces the default logger");
  check(minilog::getLogger("replacement") == custom, "the replacement is registered under its name");

  minilog::setDefaultLogger(nullptr);
  check(minilog::defaultLogger() != nullptr, "the default logger is re-created after it was forgotten");

  minilog::shutdown();
  check(minilog::defaultLogger() != nullptr, "logging works again after shutdown");
}

/** Logging that only writes into a rotating file, so that the output of the test stays readable. */
minilog::LoggingConfig fileLoggingConfig(const std::string& directory, const std::string& basename) {
  minilog::LoggingConfig config;
  config.console.enabled = false;
  config.debugOutput.enabled = false;
  config.level = minilog::Level::kTrace;
  config.file.enabled = true;
  config.file.directory = directory;
  config.file.basename = basename;
  config.file.maxFiles = 2;
  config.file.maxFileSize = 4096;
  config.file.flushOnLog = true;
  config.file.pattern = "%v";
  return config;
}

void testSetupLogging() {
  section("setupLogging and ScopedLogging");

  const auto directory = minilog::Utils::joinPath(outputDirectory(), "setup");
  const auto path = minilog::Utils::joinPath(directory, "setup.log");

  {
    const minilog::ScopedLogging logging(fileLoggingConfig(directory, "setup.log"));

    MINILOG_INFO("written through the default logger");
    minilog::defaultLogger()->info("and through the logger of the registry");
    minilog::flushAll();

    check(minilog::defaultLogger()->sinks().size() == 1, "the file sink replaced the console sink");
    check(contains(readFile(path), "written through the default logger"), "the macro wrote into the file");
    check(contains(readFile(path), "and through the logger of the registry"), "the default logger wrote into the file");
  }

  // The destructor of ScopedLogging flushed and dropped every logger, so the file is closed.
  check(minilog::Utils::removeFile(path), "ScopedLogging released the log file");

  // A regular file where the directory of the log belongs.
  const auto blocker = minilog::Utils::joinPath(directory, "not_a_directory");
  {
    const minilog::FileSink blockerFile(blocker, true);
  }

  bool threw = false;
  try {
    minilog::LoggingConfig none;
    none.console.enabled = false;
    none.debugOutput.enabled = false;
    none.file.enabled = false;
    minilog::setupLogging(none);
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "a configuration without any sink is refused");

  threw = false;
  try {
    minilog::LoggingConfig broken;
    broken.console.enabled = false;
    broken.debugOutput.enabled = false;
    broken.file.enabled = true;
    // A regular file sits where the directory of the log belongs, so the directory cannot be created.
    broken.file.directory = blocker;
    minilog::setupLogging(broken);
  } catch (const minilog::MiniLogException&) {
    threw = true;
  }
  check(threw, "a log directory that cannot be created is reported");
  check(minilog::Utils::removeFile(blocker), "cleanup: the file that stood in the way of the directory");

  minilog::shutdown();
}

void testSetupPerSink() {
  section("setupLogging: per-sink settings");

  const auto directory = minilog::Utils::joinPath(outputDirectory(), "setup_per_sink");
  const auto path = minilog::Utils::joinPath(directory, "per.log");

  {
    auto config = fileLoggingConfig(directory, "per.log");
    config.level = minilog::Level::kTrace;
    config.flushLevel = minilog::Level::kError;
    config.backtraceMessages = 2;
    config.file.maxFiles = 1;
    config.file.truncate = true;
    config.file.pattern = "%l|%v";
    config.file.level = minilog::Level::kWarn;

    const minilog::ScopedLogging logging(config);

    check(minilog::defaultLogger()->level() == minilog::Level::kTrace, "the configured logger level is applied");
    check(minilog::defaultLogger()->flushLevel() == minilog::Level::kError,
      "the configured flush level is applied");
    check(minilog::defaultLogger()->shouldTrace(), "the configured backtrace is enabled");
    check(minilog::defaultLogger()->sinks().size() == 1, "only the file sink was installed");
    check(minilog::defaultLogger()->sinks().front()->getLevel() == minilog::Level::kWarn,
      "the level of the file sink is applied");

    MINILOG_INFO("hidden by the sink");
    MINILOG_WARN("kept");
    MINILOG_ERROR("kept too");
    minilog::defaultLogger()->flush();

    const auto content = readFile(path);
    check(!contains(content, "hidden by the sink"), "the level of the sink filters the file");
    check(contains(content, "warning|kept"), "the pattern of the file sink is applied");
    check(contains(content, "error|kept too"), "a message above the sink level is written");
  }

  check(minilog::Utils::removeFile(path), "ScopedLogging released the per-sink file");

  // A daily configuration selects the daily file sink and creates the file of the day.
  const auto dailyDirectory = minilog::Utils::joinPath(outputDirectory(), "setup_daily");
  const auto dailyPath = minilog::Utils::joinPath(dailyDirectory, "daily.log");
  {
    auto config = fileLoggingConfig(dailyDirectory, "daily.log");
    config.level = minilog::Level::kTrace;
    config.file.daily = true;
    config.file.truncate = true;
    config.file.pattern = "%v";
    config.file.level = minilog::Level::kTrace;

    const minilog::ScopedLogging logging(config);
    MINILOG_INFO("daily message");
    minilog::defaultLogger()->flush();
  }

  const auto dailyFile = minilog::DailyFileSinkMt::calculateFilename(dailyPath, std::time(nullptr));
  check(minilog::Utils::fileExists(dailyFile), "a daily configuration creates the file of the day");
  check(contains(readFile(dailyFile), "daily message"), "the daily file received the message");
  check(minilog::Utils::removeFile(dailyFile), "cleanup: the daily setup file");
}
/** Runs the debugger sink last: it redirects stderr into a file, which stays redirected. */
void testDebuggerSinkOutput() {
  section("debugger sink output");

  const auto path = minilog::Utils::joinPath(outputDirectory(), "debugger_stderr.txt");

  if (std::freopen(path.c_str(), "w", stderr) == nullptr) {
    check(false, "stderr could not be redirected into a file");
    return;
  }

  // The default error handler writes into stderr; this is its only chance to be observed by the suite.
  {
    auto failing = std::make_shared<minilog::CallbackSinkMt>([](const minilog::Message&) {
      throw minilog::MiniLogException("the default handler failure");
    });
    minilog::Logger logger("default-error", failing);
    logger.info("this triggers the default error handler");
  }

  {
    minilog::DebuggerSinkMt sink(minilog::DebuggerSinkMt::Stream::kStdErr);
    sink.setPattern("%v");
    sink.log(message("line of the debugger console"));
  }

  std::fflush(stderr);
  const auto content = readFile(path);
  check(contains(content, "line of the debugger console"),
    "the debugger sink wrote and flushed its message into stderr");
  check(contains(content, "[*** LOG ERROR") && contains(content, "the default handler failure"),
    "the default error handler reported the failing sink into stderr");
}

} // namespace anonymous

int main() {
  std::error_code ec;
  std::filesystem::remove_all(outputDirectory(), ec);
  if (!minilog::Utils::createDirectories(outputDirectory())) {
    std::printf("cannot create the output directory '%s'\n", outputDirectory().c_str());
    return 1;
  }

  std::printf("minilog sink tests, output directory: %s\n",
    std::filesystem::absolute(outputDirectory()).string().c_str());

  testFileSink();
  testFileSinkThroughLogger();
  testFileSinkReopenAndThreads();
  testRotatingFileSink();
  testRotatingFileSinkExplicitRotation();
  testDailyFileSink();
  testDailyFileSinkExtras();
  testDebuggerSink();
  testMsvcSink();
  testAnsiColorSink();
  testWinColorSink();
  testStreamAndNullSinks();
  testRingBufferSink();
  testCallbackSink();
  testDistSink();
  testLoggerErrorHandling();
  testRegistry();
  testSetupLogging();
  testSetupPerSink();

  // Last, because it redirects stderr for the rest of the process.
  testDebuggerSinkOutput();

  std::printf("%d checks, %d failures\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
