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

#ifndef MINILOG_MUTEX_H
#define MINILOG_MUTEX_H

#include <mutex>

namespace minilog {

struct ConsoleMutex {
  using mutex_t = std::mutex;

  static mutex_t& mutex() {
    static mutex_t sMutex;
    return sMutex;
  }
};

struct NoMutex {
  void lock() const {
  }

  void unlock() const {
  }
};

struct NullMutex {
  using mutex_t = NoMutex;

  static mutex_t& mutex() {
    static mutex_t sMutex;
    return sMutex;
  }
};

} // namespace minilog

#endif //MINILOG_MUTEX_H
