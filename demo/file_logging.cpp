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
//  file_logging.cpp
//
//  A console plus a rotating log file, with a different pattern
//  and level for every sink. The file is written below demo_logs/
//  in the current working directory.
//
//    minilog_demo_file_logging
// =============================================================

#include <minilog/LogSetup.h>
#include <minilog/log.h>

#include <filesystem>
#include <iostream>
#include <string>

int main() {
  const std::string directory = "demo_logs";

  minilog::LoggingConfig config;
  config.level = minilog::Level::kInfo;
  // An error and everything above it flushes every sink, so the last failure is already on disk.
  config.flushLevel = minilog::Level::kError;
  config.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v";

  // The console stays short: only the level and the message.
  config.console.enabled = true;
  config.console.colorMode = minilog::ColorMode::kAutomatic;
  config.console.pattern = "%l: %v";
  config.console.level = minilog::Level::kInfo;

  // The Output window of Visual Studio is not used by this example.
  config.debugOutput.enabled = false;

  // A rotating file: three files of at most 512 bytes, named demo.log, demo_1.log and demo_2.log.
  config.file.enabled = true;
  config.file.directory = directory;
  config.file.basename = "demo.log";
  config.file.maxFiles = 3;
  config.file.maxFileSize = 512;
  config.file.truncate = true;
  config.file.flushOnLog = false;
  config.file.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v";
  config.file.level = minilog::Level::kInfo;

  {
    const minilog::ScopedLogging logging(config);

    for (int tick = 0; tick < 20; ++tick) {
      MINILOG_INFO("tick {} with a line long enough to make the file rotate", tick);
    }

    // This flushes the file right away because of config.flushLevel.
    MINILOG_ERROR("one error is enough to flush every sink");
  } // ScopedLogging flushes and closes the file here.

  std::cout << "log files in " << std::filesystem::absolute(directory) << ":\n";
  for (const auto& entry: std::filesystem::directory_iterator(directory)) {
    if (entry.is_regular_file()) {
      std::cout << "  " << entry.path().filename().string() << " (" << entry.file_size() << " bytes)\n";
    }
  }

  return 0;
}
