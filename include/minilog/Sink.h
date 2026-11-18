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

#ifndef MINILOG_SINK_H
#define MINILOG_SINK_H

#include <minilog/common.h>
#include <minilog/Formatter.h>

#include "private/minilog/Message.h"

#include <atomic>

namespace minilog {

class Sink {
public:
  virtual ~Sink() = default;

  virtual void log(const Message& msg) = 0;

  virtual void flush() = 0;

  virtual void setPattern(const std::string& pattern) = 0;

  virtual void setFormatter(std::unique_ptr<Formatter> formatter) = 0;

  /**
   * Sets the lowest level this sink logs.
   *
   * Virtual so that a sink that owns other sinks - DistSink - hands the level down to them.
   */
  virtual void setLevel(const Level level) {
    mLevel.store(level, std::memory_order_relaxed);
  }

  Level getLevel() const { return mLevel.load(std::memory_order_relaxed); }

  bool shouldLog(const Level& level) const { return mLevel.load(std::memory_order_relaxed) <= level; }

protected:
  std::atomic<Level> mLevel{ Level::kTrace };
};

} // namespace minilog

#endif //MINILOG_SINK_H
