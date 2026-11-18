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

#include "private/minilog/Utils.h"

#include <filesystem>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <share.h> // for _SH_DENYNO
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>

#include <sys/syscall.h> //Use gettid() syscall under linux to get thread id

#endif

namespace minilog {

namespace {

#if defined(_WIN32)

/**
 * The public API takes UTF-8 paths. Building a std::filesystem::path from a narrow string would read it with
 * the ANSI code page of the machine instead, which mangles every non-ASCII path on Windows.
 */
std::wstring utf8ToWide(const std::string& text) {
  if (text.empty()) {
    return {};
  }

  const auto length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) {
    return {};
  }

  std::wstring wide(static_cast<size_t>(length), L'\0');
  if (::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length) != length) {
    return {};
  }
  return wide;
}

std::string wideToUtf8(const std::wstring& text) {
  if (text.empty()) {
    return {};
  }

  const auto length =
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (length <= 0) {
    return {};
  }

  std::string utf8(static_cast<size_t>(length), '\0');
  if (::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr,
        nullptr) != length) {
    return {};
  }
  return utf8;
}

std::filesystem::path toPath(const std::string& utf8Path) {
  return std::filesystem::path(utf8ToWide(utf8Path));
}

std::string toUtf8(const std::filesystem::path& path) {
  return wideToUtf8(path.native());
}

#else

std::filesystem::path toPath(const std::string& utf8Path) {
  return std::filesystem::path(utf8Path);
}

std::string toUtf8(const std::filesystem::path& path) {
  return path.native();
}

#endif

void assignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

} // namespace anonymous

Clock::time_point Utils::now() noexcept {
#if defined __linux__ && defined MINILOG_CLOCK_COARSE
  timespec ts;
  ::clock_gettime(CLOCK_REALTIME_COARSE, &ts);
  return std::chrono::time_point<Clock, typename Clock::duration>(std::chrono::duration_cast<typename Clock::duration>(
          std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec)));
#else
  return Clock::now();
#endif
}

std::tm Utils::localtime(const std::time_t& time_tt) noexcept {
#ifdef _WIN32
  std::tm tm;
  ::localtime_s(&tm, &time_tt);
#else
  std::tm tm;
  ::localtime_r(&time_tt, &tm);
#endif
  return tm;
}

std::tm Utils::localtime() noexcept {
  const std::time_t now_t = ::time(nullptr);
  return localtime(now_t);
}

std::tm Utils::gmtime(const std::time_t& time_tt) noexcept {
#ifdef _WIN32
  std::tm tm;
  ::gmtime_s(&tm, &time_tt);
#else
  std::tm tm;
  ::gmtime_r(&time_tt, &tm);
#endif
  return tm;
}

std::tm Utils::gmtime() noexcept {
  const std::time_t now_t = ::time(nullptr);
  return gmtime(now_t);
}

/**
 * Return current thread id as size_t
 *
 * It exists because the std::this_thread::get_id() is much slower(especially under VS 2013)
 */
inline size_t _thread_id() noexcept {
#ifdef _WIN32
  return static_cast<size_t>(::GetCurrentThreadId());
#elif defined(__linux__)
#if defined(__ANDROID__) && defined(__ANDROID_API__) && (__ANDROID_API__ < 21)
#define SYS_gettid __NR_gettid
#endif
  return static_cast<size_t>(::syscall(SYS_gettid));
#elif defined(_AIX)
  struct __pthrdsinfo buf;
  int reg_size = 0;
  pthread_t pt = pthread_self();
  int retval = pthread_getthrds_np(&pt, PTHRDSINFO_QUERY_TID, &buf, sizeof(buf), NULL, &reg_size);
  int tid = (!retval) ? buf.__pi_tid : 0;
  return static_cast<size_t>(tid);
#elif defined(__DragonFly__) || defined(__FreeBSD__)
  return static_cast<size_t>(::pthread_getthreadid_np());
#elif defined(__NetBSD__)
  return static_cast<size_t>(::_lwp_self());
#elif defined(__OpenBSD__)
  return static_cast<size_t>(::getthrid());
#elif defined(__sun)
  return static_cast<size_t>(::thr_self());
#elif __APPLE__
  uint64_t tid;
  // There is no pthread_threadid_np prior to Mac OS X 10.6, and it is not supported on any PPC,
  // including 10.6.8 Rosetta. __POWERPC__ is Apple-specific define encompassing ppc and ppc64.
#ifdef MAC_OS_X_VERSION_MAX_ALLOWED
  {
#if (MAC_OS_X_VERSION_MAX_ALLOWED < 1060) || defined(__POWERPC__)
    tid = pthread_mach_thread_np(pthread_self());
#elif MAC_OS_X_VERSION_MIN_REQUIRED < 1060
    if (&pthread_threadid_np) {
      pthread_threadid_np(nullptr, &tid);
    } else {
      tid = pthread_mach_thread_np(pthread_self());
    }
#else
    pthread_threadid_np(nullptr, &tid);
#endif
  }
#else
  pthread_threadid_np(nullptr, &tid);
#endif
  return static_cast<size_t>(tid);
#else // Default to standard C++11 (other Unix)
  return static_cast<size_t>(std::hash<std::thread::id>()(std::this_thread::get_id()));
#endif
}

size_t Utils::threadId() noexcept {
#if defined(MINILOG_NO_TLS)
  return _thread_id();
#else
  static thread_local const size_t tid = _thread_id();
  return tid;
#endif
}

int Utils::pid() noexcept {
#ifdef _WIN32
  return static_cast<int>(::GetCurrentProcessId());
#else
  return static_cast<int>(::getpid());
#endif
}

int Utils::utcMinutesOffset(const std::tm& tm) noexcept {
#ifdef _WIN32
  std::tm local_tm = tm; // copy since mktime might adjust it (normalize dates, set tm_isdst)
  const std::time_t local_time_t = std::mktime(&local_tm);
  if (local_time_t == -1) {
    return 0; // fallback
  }

  const std::time_t utc_time_t = _mkgmtime(&local_tm);
  if (utc_time_t == -1) {
    return 0; // fallback
  }
  const auto offsetSeconds = utc_time_t - local_time_t;
  return static_cast<int>(offsetSeconds / 60);
#else
  return static_cast<int>(tm.tm_gmtoff / 60);
#endif
}

void Utils::usleep(const unsigned int milliseconds) noexcept {
#ifdef _WIN32
  ::Sleep(milliseconds);
#else
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#endif
}

std::FILE* Utils::openFile(const std::string& filePath, const char* mode) {
#if defined(_WIN32)
  // _wfsopen() rather than _wfopen_s(): the share mode of _wfopen_s denies every other reader, which makes the
  // log file of a running process impossible to open in an editor or to follow with "tail -f". Sharing it keeps
  // the file readable while the process logs into it, exactly like fopen() does on the other platforms.
  wchar_t wideMode[8] = {};
  for (size_t i = 0; i + 1 < 8 && mode[i] != '\0'; ++i) {
    wideMode[i] = static_cast<wchar_t>(static_cast<unsigned char>(mode[i]));
  }

  return ::_wfsopen(utf8ToWide(filePath).c_str(), wideMode, _SH_DENYNO);
#else
  return std::fopen(filePath.c_str(), mode);
#endif
}

bool Utils::createDirectories(const std::string& directory, std::string* error) {
  if (directory.empty()) {
    return true;
  }

  std::error_code ec;
  const auto path = toPath(directory);
  if (std::filesystem::is_directory(path, ec)) {
    return true;
  }

  // is_directory() reports a missing directory through the error code, which is not a failure here.
  ec.clear();
  std::filesystem::create_directories(path, ec);
  if (ec) {
    assignError(error, "cannot create the directory '" + directory + "': " + ec.message());
    return false;
  }
  return true;
}

bool Utils::fileExists(const std::string& filePath) {
  std::error_code ec;
  return std::filesystem::is_regular_file(toPath(filePath), ec);
}

bool Utils::renameFile(const std::string& source, const std::string& target, std::string* error) {
  const auto sourcePath = toPath(source);
  const auto targetPath = toPath(target);

  std::error_code ec;
  // Check the source before the target is touched: without this, a rename of a missing source removed the
  // existing target and then failed, which is the worst of both outcomes for the caller.
  if (!std::filesystem::exists(sourcePath, ec)) {
    if (ec) {
      assignError(error, "cannot rename '" + source + "': " + ec.message());
    } else {
      assignError(error, "cannot rename '" + source + "': the source does not exist");
    }
    return false;
  }

  if (std::filesystem::exists(targetPath, ec)) {
    ec.clear();
    std::filesystem::remove(targetPath, ec);
    if (ec) {
      assignError(error, "cannot replace '" + target + "': " + ec.message());
      return false;
    }
  }

  ec.clear();
  std::filesystem::rename(sourcePath, targetPath, ec);
  if (ec) {
    assignError(error, "cannot rename '" + source + "' to '" + target + "': " + ec.message());
    return false;
  }
  return true;
}

bool Utils::removeFile(const std::string& filePath, std::string* error) {
  std::error_code ec;
  const auto removed = std::filesystem::remove(toPath(filePath), ec);
  if (ec) {
    assignError(error, "cannot remove '" + filePath + "': " + ec.message());
    return false;
  }
  if (!removed) {
    // Nothing was there to remove, which is a failure for a caller that expects the file to be gone.
    assignError(error, "cannot remove '" + filePath + "': the file does not exist");
    return false;
  }
  return true;
}

std::string Utils::dirname(const std::string& filePath) {
  const auto position = filePath.find_last_of("/\\");
  return position == std::string::npos ? std::string() : filePath.substr(0, position + 1);
}

std::string Utils::basename(const std::string& filePath) {
  const auto position = filePath.find_last_of("/\\");
  return position == std::string::npos ? filePath : filePath.substr(position + 1);
}

std::string Utils::stem(const std::string& filePath) {
  const auto name = basename(filePath);
  const auto position = name.find_last_of('.');
  // A leading dot names a hidden file, not an extension.
  return (position == std::string::npos || position == 0) ? name : name.substr(0, position);
}

std::string Utils::extension(const std::string& filePath) {
  const auto name = basename(filePath);
  const auto position = name.find_last_of('.');
  return (position == std::string::npos || position == 0) ? std::string() : name.substr(position);
}

std::string Utils::joinPath(const std::string& directory, const std::string& name) {
  if (directory.empty()) {
    return name;
  }

  const auto last = directory.back();
  if (last == '/' || last == '\\') {
    return directory + name;
  }
  return directory + "/" + name;
}

std::string Utils::formatTime(const std::time_t& time, const char* format) {
  const auto tmTime = localtime(time);
  char buffer[64];
  const auto written =
    std::strftime(buffer, sizeof(buffer), (format != nullptr && format[0] != '\0') ? format : "%Y-%m-%d", &tmTime);
  return written == 0 ? std::string() : std::string(buffer, written);
}

} // namespace minilog
