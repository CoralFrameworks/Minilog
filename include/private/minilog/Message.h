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

#ifndef MINILOG_LOG_MSG_H
#define MINILOG_LOG_MSG_H

#include <minilog/common.h>

#include <chrono>

namespace minilog {

struct Message {
  Message() = default;

  Message(const std::string_view name, const Level level, const std::string_view message)
    : Message(Source(), name, level, message) {
  }

  Message(const Source& source, const std::string_view name, const Level level, const std::string_view message)
    : Message(Clock::now(), source, name, level, message) {
  }

  Message(Clock::time_point time, const Source& source, std::string_view name, Level level,
    std::string_view message);

  Message(const Message& other) = default;
  Message(Message&& other) noexcept = default;
  Message& operator=(const Message& other) = default;
  Message& operator=(Message&& other) noexcept = default;

  std::string_view name;
  Level level{ Level::kOff };
  Clock::time_point time;
  size_t thread_id{ 0 };

  mutable size_t colorRangeStart{ 0 };
  mutable size_t colorRangeEnd{ 0 };

  Source source;
  std::string_view payload;
};

class MessageBuffer : public Message {
public:
  MessageBuffer() = default;
  explicit MessageBuffer(const Message& msg);

  MessageBuffer(const MessageBuffer& other);
  MessageBuffer(MessageBuffer&& other) noexcept;
  MessageBuffer& operator=(const MessageBuffer& other);
  MessageBuffer& operator=(MessageBuffer&& other) noexcept;

private:
  std::string buffer;

  void updateStringViews();
};


} // namespace minilog

#endif // MINILOG_LOG_MSG_H
