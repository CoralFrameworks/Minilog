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

#ifndef MINILOG_UTILS_H
#define MINILOG_UTILS_H

#include <minilog/common.h>

#include <cstdio>
#include <ctime>
#include <string>

namespace minilog {

class Utils {
public:
  static const char* defaultEol() { return MINILOG_EOL; }

  static Clock::time_point now() noexcept;

  static std::tm localtime(const std::time_t& time_tt) noexcept;

  static std::tm localtime() noexcept;

  static std::tm gmtime(const std::time_t& time_tt) noexcept;

  static std::tm gmtime() noexcept;

  static size_t threadId() noexcept;

  static int pid() noexcept;

  static int utcMinutesOffset(const std::tm& tm = localtime()) noexcept;

  static void usleep(unsigned int milliseconds) noexcept;

  // ------------------------------------------------------------------------------------------------
  // Files
  //
  // Every path taken by these helpers is UTF-8 encoded, on Windows as well, where the path is converted to
  // UTF-16 before it reaches the file system API. That keeps log files of a project whose path contains
  // non-ASCII characters working.
  // ------------------------------------------------------------------------------------------------

  /**
   * Opens a file with the given C mode ("ab", "wb", ...).
   *
   * @return The stream, or nullptr when the file cannot be opened.
   */
  static std::FILE* openFile(const std::string& filePath, const char* mode);

  /**
   * Creates a directory with all its missing parents; an existing directory counts as success.
   *
   * @param error Filled with a description of the failure when the function returns false.
   */
  static bool createDirectories(const std::string& directory, std::string* error = nullptr);

  static bool fileExists(const std::string& filePath);

  /**
   * Renames a file, replacing the target when it already exists.
   *
   * The target is removed first because a rename over an existing file is not atomic on Windows anyway; it
   * fails with a sharing violation while the target is open anywhere in the process.
   *
   * @param error Filled with a description of the failure when the function returns false.
   */
  static bool renameFile(const std::string& source, const std::string& target, std::string* error = nullptr);

  /**
   * Removes a file. Removing a file that does not exist fails, use fileExists() first.
   *
   * @param error Filled with a description of the failure when the function returns false.
   */
  static bool removeFile(const std::string& filePath, std::string* error = nullptr);

  /**
   * The directory part of the path, including the trailing separator, or an empty string.
   *
   * "logs/coral.log" -> "logs/", "coral.log" -> "".
   */
  static std::string dirname(const std::string& filePath);

  /** The file name part of the path, extension included: "logs/coral.log" -> "coral.log". */
  static std::string basename(const std::string& filePath);

  /** The file name without its extension: "logs/coral.log" -> "coral". */
  static std::string stem(const std::string& filePath);

  /** The extension including the leading dot, or an empty string: "logs/coral.log" -> ".log". */
  static std::string extension(const std::string& filePath);

  /** Joins a directory and a name with a separator, unless the directory already ends with one. */
  static std::string joinPath(const std::string& directory, const std::string& name);

  /**
   * Formats a time with std::strftime.
   *
   * @param format The strftime pattern, "%Y-%m-%d" when it is empty.
   * @return The formatted time, or an empty string when the pattern is invalid.
   */
  static std::string formatTime(const std::time_t& time, const char* format);

private:
  Utils() = default;
  ~Utils() = default;
};

} // namespace minilog

#endif // MINILOG_UTILS_H
