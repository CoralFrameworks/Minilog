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

#ifndef MINILOG_MDC_H
#define MINILOG_MDC_H

#include <minilog/common.h>

#include <map>
#include <string>

namespace minilog {

/**
 * mdc is a simple map of key->string values stored in thread local storage whose content will
 * be printed by the loggers. Note: Not supported in async mode (thread local storage - so the async
 * thread pool have different copy).
 */
class mdc {
public:
  using map_t = std::map<std::string, std::string>;

  static void put(const std::string& key, const std::string& value) {
    getContext()[key] = value;
  }

  static std::string get(const std::string& key) {
    const auto& context = getContext();
    if (const auto it = context.find(key); it != context.end()) {
      return it->second;
    }
    return "";
  }

  static void remove(const std::string& key) {
    getContext().erase(key);
  }

  static void clear() {
    getContext().clear();
  }

  /**
   * The context of the calling thread.
   *
   * Returned by reference on purpose: the mutators above have to change the thread-local map itself, and a
   * by-value return made every put/remove/clear operate on a temporary copy that was thrown away at once.
   */
  static map_t& getContext() {
    static thread_local map_t context;
    return context;
  }
};

} // namespace minilog

#endif // MINILOG_MDC_H
