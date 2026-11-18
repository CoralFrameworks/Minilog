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
//  quickstart.cpp
//
//  The smallest useful setup: a colored console, a pattern and the
//  log macros. ScopedLogging flushes and releases everything at the
//  end of main().
//
//    minilog_demo_quickstart
// =============================================================

#include <minilog/LogSetup.h>
#include <minilog/log.h>

#include <string>

int main() {
  minilog::LoggingConfig config;
  config.level = minilog::Level::kDebug;
  // %+ prints the time, the logger name, the level, the source and the message.
  config.pattern = "%+";
  config.console.enabled = true;
  config.console.colorMode = minilog::ColorMode::kAutomatic;
  config.console.useStdErr = false;

  // This example only writes to the console, so the other outputs stay off.
  config.debugOutput.enabled = false;
  config.file.enabled = false;

  // The constructor installs the configuration; the destructor calls minilog::shutdown().
  const minilog::ScopedLogging logging(config);

  // The trace macro is compiled in a Debug build and filtered here by the debug level.
  MINILOG_TRACE("a trace message is filtered at the debug level");
  MINILOG_DEBUG("a debug message with a value: {}", 42);
  MINILOG_INFO("the player started at {}", std::string("12:00"));
  MINILOG_WARN("the buffer is {} % full", 87);
  MINILOG_ERROR("the decoder refused the stream: {}", "unsupported codec");

  // The free functions and the default logger are the same object the macros use.
  minilog::defaultLogger()->info("the same message through the default logger");
  minilog::info("and through the free function");

  return 0;
}
