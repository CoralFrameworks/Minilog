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

#ifndef MINILOG_SINKS_ANDROID_SINK_H
#define MINILOG_SINKS_ANDROID_SINK_H

#if defined(__ANDROID__)

#include <minilog/common.h>
#include <minilog/sinks/BaseSink.h>

#include "private/minilog/FmtHelper.h"
#include "private/minilog/Utils.h"

#include <android/log.h>

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>

#if !defined(MINILOG_ANDROID_RETRIES)
#define MINILOG_ANDROID_RETRIES 2
#endif

#if !defined(MINILOG_TAG)
#define MINILOG_TAG "minilog"
#endif

namespace minilog {

template<typename Mutex, int BufferID = log_id::LOG_ID_MAIN>
class AndroidSink final : public BaseSink<Mutex> {
public:
  explicit AndroidSink(std::string tag = MINILOG_TAG, const bool useRawMsg = false) noexcept
    : mTag(std::move(tag)),
      mUseRawMsg(useRawMsg) {
  }

protected:
  void onSink(const Message& msg) override {
    const auto priority = convertToAndroidLogPriority(msg.level);
    std::string formatted;
    if (mUseRawMsg) {
      FmtHelper::appendString(msg.payload, formatted);
    } else {
      BaseSink<Mutex>::mFormatter->format(msg, formatted);
    }
    formatted.push_back('\0');
    const char* msg_output = formatted.data();

    // See system/core/liblog/logger_write.c for explanation of return value
    int ret = android_log(priority, mTag.c_str(), msg_output);
    if (ret == -EPERM) {
      return; // !__android_log_is_loggable
    }
    int retry_count = 0;
    while ((ret == -11 /*EAGAIN*/) && (retry_count < MINILOG_ANDROID_RETRIES)) {
      Utils::usleep(5);
      ret = android_log(priority, mTag.c_str(), msg_output);
      retry_count++;
    }

    if (ret < 0) {
      MINILOG_THROW(MiniLogException("logging to Android failed", ret));
    }
  }

  void onFlush() override {
    // do nothing
  }

private:
  std::string mTag;
  bool mUseRawMsg;

  // There might be liblog versions used, that do not support __android_log_buf_write. So we only
  // compile and link against
  // __android_log_buf_write, if user explicitly provides a non-default log buffer. Otherwise,
  // when using the default log buffer, always log via __android_log_write.
  template<int ID = BufferID>
  typename std::enable_if<ID == static_cast<int>(log_id::LOG_ID_MAIN), int>::type
  android_log(int priority, const char* tag, const char* text) {
    return __android_log_write(priority, tag, text);
  }

  template<int ID = BufferID>
  typename std::enable_if<ID != static_cast<int>(log_id::LOG_ID_MAIN), int>::type
  android_log(int priority, const char* tag, const char* text) {
    return __android_log_buf_write(ID, priority, tag, text);
  }

  static android_LogPriority convertToAndroidLogPriority(const Level level) {
    switch (level) {
      case Level::kTrace:
        return ANDROID_LOG_VERBOSE;
      case Level::kDebug:
        return ANDROID_LOG_DEBUG;
      case Level::kInfo:
        return ANDROID_LOG_INFO;
      case Level::kWarn:
        return ANDROID_LOG_WARN;
      case Level::kError:
        return ANDROID_LOG_ERROR;
      case Level::kCritical:
        return ANDROID_LOG_FATAL;
      default:
        return ANDROID_LOG_DEFAULT;
    }
  }
};

using AndroidDefaultSink = AndroidSink<std::mutex>;

} // namespace minilog

#endif // defined(__ANDROID__)

#endif //MINILOG_SINKS_ANDROID_SINK_H
