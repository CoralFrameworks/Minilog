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
//  backtrace.cpp
//
//  Captures the messages below the level of the logger and dumps
//  them when something goes wrong. Only the error is printed while
//  the demo runs; the dump shows what led to it.
//
//    minilog_demo_backtrace
// =============================================================

#include <minilog/Logger.h>
#include <minilog/common.h>
#include <minilog/sinks/OStreamSink.h>

#include <iostream>
#include <memory>

int main() {
  auto sink = std::make_shared<minilog::OStreamSinkMt>(std::cout);
  sink->setPattern("%l: %v");

  minilog::Logger logger("backtrace-demo", sink);

  // The logger writes errors only, but the tracer records the last three messages of every level.
  logger.setLevel(minilog::Level::kError);
  logger.enableBacktrace(3);

  logger.trace("loading the asset pack");
  logger.debug("the asset pack contains 120 entries");
  logger.info("the playback pipeline is ready");
  logger.warn("the first frame took 40 ms");
  logger.trace("this message is not kept any more");

  logger.error("the playback failed");
  logger.dumpBacktrace();

  return 0;
}
