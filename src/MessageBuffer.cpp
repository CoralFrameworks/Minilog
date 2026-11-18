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

#include "private/minilog/Message.h"

namespace minilog {

MessageBuffer::MessageBuffer(const Message& msg)
  : Message(msg) {
  if (!name.empty()) {
    buffer.append(name.data(), name.size());
  }
  if (!payload.empty()) {
    buffer.append(payload.data(), payload.size());
  }
  updateStringViews();
}

MessageBuffer::MessageBuffer(const MessageBuffer& other)
  : Message(other) {
  if (!other.name.empty()) {
    buffer.append(other.name.data(), other.name.size());
  }
  if (!other.payload.empty()) {
    buffer.append(other.payload.data(), other.payload.size());
  }
  updateStringViews();
}

MessageBuffer::MessageBuffer(MessageBuffer&& other) noexcept
  : Message{ other },
    buffer{ std::move(other.buffer) } {
  updateStringViews();
}

MessageBuffer& MessageBuffer::operator=(const MessageBuffer& other) {
  if (this == &other) {
    return *this;
  }

  Message::operator=(other);
  buffer.clear();
  if (!other.buffer.empty()) {
    buffer.append(other.buffer.data(), other.buffer.size());
  }
  updateStringViews();
  return *this;
}

MessageBuffer& MessageBuffer::operator=(MessageBuffer&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  Message::operator=(other);
  buffer = std::move(other.buffer);
  updateStringViews();
  return *this;
}

void MessageBuffer::updateStringViews() {
  name = std::string_view{ buffer.data(), name.size() };
  payload = std::string_view{ buffer.data() + name.size(), payload.size() };
}

} // namespace minilog
