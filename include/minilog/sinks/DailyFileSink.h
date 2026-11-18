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

#ifndef MINILOG_SINKS_DAILY_FILE_SINK_H
#define MINILOG_SINKS_DAILY_FILE_SINK_H

#include <minilog/common.h>
#include <minilog/sinks/FileSink.h>

#include "private/minilog/Utils.h"

#include <chrono>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>

namespace minilog {

/**
 * Writes into a file whose name contains the date, and moves to a new file once a day.
 *
 * The name of the file is built like the rotating one, with the local date instead of an index:
 * ("logs/coral.log", 2026-01-02) -> "logs/coral_2026-01-02.log". A file that already holds the log of the same
 * day is appended to, unless `truncate` was requested.
 *
 * The rotation is driven by the timestamp of the message that is being written, so a message that carries a
 * time past the rotation ends up in the file of its own day, and the day boundary is crossed exactly once.
 * The date is a local date; the daylight saving offset of the day is taken care of by the runtime.
 *
 * @tparam Mutex std::mutex for a sink shared by several threads, NoMutex for a sink used by one thread only.
 */
template<typename Mutex>
class DailyFileSink final : public BasicFileSink<Mutex> {
public:
  /**
   * Opens the file of the current day.
   *
   * @param baseFilename Name the date is inserted into, UTF-8 encoded.
   * @param rotationHour Hour of the day (0-23) at which the file changes.
   * @param rotationMinute Minute of the hour (0-59) at which the file changes.
   * @param truncate Truncate the file of the day instead of appending to it.
   *
   * @throws MiniLogException when the arguments are invalid or the file cannot be opened.
   */
  DailyFileSink(const std::string& baseFilename, const int rotationHour, const int rotationMinute,
    const bool truncate = false)
    : BasicFileSink<Mutex>(calculateFilename(baseFilename, std::time(nullptr)), truncate),
      mBaseFilename(baseFilename),
      mRotationHour(rotationHour),
      mRotationMinute(rotationMinute),
      mTruncate(truncate) {
    if (rotationHour < 0 || rotationHour > 23) {
      throw MiniLogException(
        "daily file sink '" + baseFilename + "': the rotation hour must be between 0 and 23, got " +
        std::to_string(rotationHour));
    }
    if (rotationMinute < 0 || rotationMinute > 59) {
      throw MiniLogException(
        "daily file sink '" + baseFilename + "': the rotation minute must be between 0 and 59, got " +
        std::to_string(rotationMinute));
    }

    mNextRotation = nextRotationTimePoint(Clock::now(), rotationHour, rotationMinute);
  }

  ~DailyFileSink() override = default;

  DailyFileSink(const DailyFileSink&) = delete;
  DailyFileSink& operator=(const DailyFileSink&) = delete;

  /**
   * Moves to the file of the current day right away.
   *
   * Must not be called from a callback that runs while this sink is logging, the mutex is not recursive.
   */
  void rotateNow() {
    std::lock_guard<Mutex> lock(this->mMutex);
    rotateLocked(Clock::now());
  }

  /** The time at which the sink will move to the next file. */
  [[nodiscard]] Clock::time_point nextRotation() const {
    std::lock_guard<Mutex> lock(this->mMutex);
    return mNextRotation;
  }

  [[nodiscard]] const std::string& baseFilename() const noexcept { return mBaseFilename; }

  [[nodiscard]] int rotationHour() const noexcept { return mRotationHour; }

  [[nodiscard]] int rotationMinute() const noexcept { return mRotationMinute; }

  /**
   * The name of the file that holds the messages of the day of `time`.
   *
   * The date is the local date, formatted as %Y-%m-%d.
   */
  static std::string calculateFilename(const std::string& baseFilename, const std::time_t& time) {
    return Utils::dirname(baseFilename) + Utils::stem(baseFilename) + "_" + Utils::formatTime(time, "%Y-%m-%d") +
           Utils::extension(baseFilename);
  }

  /**
   * The next time a rotation happens after `now`, at the given hour and minute of the local day.
   */
  static Clock::time_point nextRotationTimePoint(const Clock::time_point now, const int hour, const int minute) {
    std::tm date = Utils::localtime(Clock::to_time_t(now));
    date.tm_hour = hour;
    date.tm_min = minute;
    date.tm_sec = 0;
    // Let mktime() work out the daylight saving state of that date instead of inheriting the one of `now`.
    date.tm_isdst = -1;

    const auto today = std::mktime(&date);
    if (today == static_cast<std::time_t>(-1)) {
      // The local time is not representable; a plain day keeps the sink rotating.
      return now + std::chrono::hours(24);
    }

    const auto rotation = Clock::from_time_t(today);
    return rotation > now ? rotation : rotation + std::chrono::hours(24);
  }

protected:
  void onWrite(const Message& msg, const std::string_view formatted) override {
    if (msg.time >= mNextRotation) {
      rotateLocked(msg.time);
    }

    BasicFileSink<Mutex>::onWrite(msg, formatted);
  }

private:
  const std::string mBaseFilename;
  const int mRotationHour;
  const int mRotationMinute;
  const bool mTruncate;
  Clock::time_point mNextRotation{};

  /** Moves to the file of `time`. The sink mutex must be held by the caller. */
  void rotateLocked(const Clock::time_point time) {
    const auto filename = calculateFilename(mBaseFilename, Clock::to_time_t(time));

    // RAII: closing first releases the previous day before the new file is created, so a failing open cannot
    // leak the stream that is no longer written into.
    this->mFile.close();
    this->mFile.open(filename, mTruncate);

    mNextRotation = nextRotationTimePoint(time, mRotationHour, mRotationMinute);
  }
};

/** Daily file sink for loggers shared by several threads. */
using DailyFileSinkMt = DailyFileSink<std::mutex>;

/** Daily file sink for a logger that lives on a single thread. */
using DailyFileSinkSt = DailyFileSink<NoMutex>;

/** The daily file sink used by default. */
using DailyFile = DailyFileSinkMt;

} // namespace minilog

#endif // MINILOG_SINKS_DAILY_FILE_SINK_H
