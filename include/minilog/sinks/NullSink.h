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

#ifndef MINILOG_SINKS_NULL_SINK_H
#define MINILOG_SINKS_NULL_SINK_H

#include <minilog/common.h>
#include <minilog/sinks/BaseSink.h>

#include "private/minilog/mutex.h"

#include <mutex>

namespace minilog {

/**
 * Throws every message away.
 *
 * Useful to disable one output of a configuration without rebuilding the logger, and to measure what the
 * formatting of a message costs in a benchmark.
 *
 * @tparam Mutex std::mutex for a sink shared by several threads, NoMutex for a sink used by one thread only.
 */
template<typename Mutex>
class NullSink final : public BaseSink<Mutex> {
public:
  NullSink() = default;

  ~NullSink() override = default;

  NullSink(const NullSink&) = delete;
  NullSink& operator=(const NullSink&) = delete;

protected:
  void onSink(const Message& /*msg*/) override {
    // Nothing to do: the message is dropped.
  }

  void onFlush() override {
    // Nothing to flush.
  }
};

/** Null sink for loggers shared by several threads. */
using NullSinkMt = NullSink<std::mutex>;

/** Null sink for a logger that lives on a single thread. */
using NullSinkSt = NullSink<NoMutex>;

/** The null sink used by default. */
using Null = NullSinkMt;

} // namespace minilog

#endif // MINILOG_SINKS_NULL_SINK_H
