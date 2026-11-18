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
//  custom_sinks.cpp
//
//  Builds a logger out of three sinks: a stream for the developer,
//  a ring buffer that keeps the last messages for a crash report,
//  and a callback that counts the errors. DistSink forwards every
//  message to all of them.
//
//    minilog_demo_custom_sinks
// =============================================================

#include <minilog/Logger.h>
#include <minilog/Sink.h>
#include <minilog/common.h>
#include <minilog/sinks/CallbackSink.h>
#include <minilog/sinks/DistSink.h>
#include <minilog/sinks/OStreamSink.h>
#include <minilog/sinks/RingBufferSink.h>

#include <iostream>
#include <memory>

int main() {
  // The developer sees the level and the message.
  auto console = std::make_shared<minilog::OStreamSinkMt>(std::cout);
  console->setPattern("%l: %v");

  // The last five formatted messages are kept in memory for a diagnosis after a failure.
  auto tail = std::make_shared<minilog::RingBufferSinkMt>(5);
  tail->setPattern("[%l] %v");

  // The callback receives the Message, not the formatted text, so it can inspect the level.
  int errorCount = 0;
  auto counter = std::make_shared<minilog::CallbackSinkMt>([&errorCount](const minilog::Message& msg) {
    if (msg.level >= minilog::Level::kError) {
      ++errorCount;
    }
  });

  auto dist = std::make_shared<minilog::DistSinkMt>();
  dist->addSink(console);
  dist->addSink(tail);
  dist->addSink(counter);
  // The level of the dist sink is handed down to every contained sink.
  dist->setLevel(minilog::Level::kDebug);

  minilog::Logger logger("custom-demo", dist);
  logger.setLevel(minilog::Level::kDebug);

  logger.debug("starting the decoder");
  logger.info("loaded {} items", 3);
  logger.warn("the cache is {} % full", 91);
  logger.error("the decoder refused the stream: {}", "stream 2");
  logger.error("a second error");
  logger.flush();

  std::cout << "\nerror count from the callback: " << errorCount << "\n";
  std::cout << "the last " << tail->size() << " formatted messages:\n";
  for (const auto& line: tail->lastFormatted()) {
    std::cout << "  " << line;
  }

  return 0;
}
