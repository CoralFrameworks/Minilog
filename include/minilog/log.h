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

#ifndef MINILOG_LOG_H
#define MINILOG_LOG_H

#include <minilog/Logger.h>

#include <memory>

namespace minilog {

/**
 * Initialize and register a logger, formatter and flush level will be set according the global settings.
 *
 * Useful for initializing manually created loggers with the global settings.
 */
void initializeLogger(std::shared_ptr<Logger> logger);

/**
 * Return an existing logger or nullptr if a logger with such a name doesn't exist.
 */
std::shared_ptr<Logger> getLogger(const std::string& name);

/**
 * Set global formatter. Each sink in each logger will get a clone of this object
 */
void setFormatter(std::unique_ptr<Formatter> formatter);

/**
 * Set global format string.
 */
void setPattern(std::string pattern, TimeZone timeZone = TimeZone::kLocal);

/**
 * Enable global backtrace support
 */
void enableBacktrace(size_t n_messages);

/**
 * Disable global backtrace support
 */
void disableBacktrace();

/**
 * Call dump backtrace on default logger
 */
void dumpBacktrace();

/**
 * Set a global flush level
 */
void flushLevel(Level level);

/**
 * Set the level of every registered logger; a logger registered later starts at this level.
 */
void setLevel(Level level);

/**
 * Flush every registered logger.
 */
void flushAll();

/**
 * Remove every registered logger, which releases their sinks - a log file is closed right away.
 *
 * The default logger is re-created on demand, so logging after this call keeps working.
 */
void dropAll();

/**
 * Flush and remove every logger.
 *
 * Call it before the process exits, or before the log files are archived, so that nothing stays in the buffers
 * of the sinks.
 */
void shutdown();

/**
 * Register the given logger with the given name
 *
 * Will throw if a logger with the same name already exists.
 */
void registerLogger(std::shared_ptr<Logger> logger);

/**
 * Register the given logger with the given name
 *
 * Will replace any existing logger with the same name.
 */
void registerOrReplace(std::shared_ptr<Logger> logger);

/**
 * Returns the default logger.
 */
std::shared_ptr<Logger> defaultLogger();

/**
 * Register the default logger.
 */
void setDefaultLogger(std::shared_ptr<Logger> logger);

template<typename... Args>
inline void log(Source source, Level level, format_string_t<Args...> fmt, Args&&... args) {
  defaultLogger()->log(source, level, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
inline void log(Level level, format_string_t<Args...> fmt, Args&&... args) {
  defaultLogger()->log(Source{}, level, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
inline void track(format_string_t<Args...> fmt, Args&&... args) {
  defaultLogger()->trace(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
inline void debug(format_string_t<Args...> fmt, Args&&... args) {
  defaultLogger()->debug(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
inline void info(format_string_t<Args...> fmt, Args&&... args) {
  defaultLogger()->info(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
inline void warn(format_string_t<Args...> fmt, Args&&... args) {
  defaultLogger()->warn(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
inline void error(format_string_t<Args...> fmt, Args&&... args) {
  defaultLogger()->error(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
inline void critical(format_string_t<Args...> fmt, Args&&... args) {
  defaultLogger()->critical(fmt, std::forward<Args>(args)...);
}

template<typename T>
inline void log(Source source, Level level, const T& msg) {
  defaultLogger()->log(source, level, msg);
}

template<typename T>
inline void log(Level level, const T& msg) {
  defaultLogger()->log(level, msg);
}

template<typename T>
inline void track(const T& msg) {
  defaultLogger()->trace(msg);
}

template<typename T>
inline void debug(const T& msg) {
  defaultLogger()->debug(msg);
}

template<typename T>
inline void info(const T& msg) {
  defaultLogger()->info(msg);
}

template<typename T>
inline void warn(const T& msg) {
  defaultLogger()->warn(msg);
}

template<typename T>
inline void error(const T& msg) {
  defaultLogger()->error(msg);
}

template<typename T>
inline void critical(const T& msg) {
  defaultLogger()->critical(msg);
}

} // namespace minilog

#ifndef MINILOG_NO_SOURCE
#define MINILOG_LOGGER_CALL(logger, level, ...)                                                 \
  (logger)->log(minilog::Source{ __FILE__, __LINE__, __FUNCTION__ }, level, __VA_ARGS__)
#else
#define MINILOG_LOGGER_CALL(logger, level, ...) (logger)->log(minilog::Source(), level, __VA_ARGS__)
#endif

#if MINILOG_ACTIVE_LEVEL <= MINILOG_LEVEL_TRACE
#define MINILOG_LOGGER_TRACE(logger, ...) MINILOG_LOGGER_CALL(logger, minilog::Level::kTrace, __VA_ARGS__)
#define MINILOG_TRACE(...) MINILOG_LOGGER_TRACE(minilog::defaultLogger(), __VA_ARGS__)
#else
#define MINILOG_LOGGER_TRACE(logger, ...) (void) 0
#define MINILOG_TRACE(...) (void) 0
#endif

#if MINILOG_ACTIVE_LEVEL <= MINILOG_LEVEL_DEBUG
#define MINILOG_LOGGER_DEBUG(logger, ...) MINILOG_LOGGER_CALL(logger, minilog::Level::kDebug, __VA_ARGS__)
#define MINILOG_DEBUG(...) MINILOG_LOGGER_DEBUG(minilog::defaultLogger(), __VA_ARGS__)
#else
#define MINILOG_LOGGER_DEBUG(logger, ...) (void) 0
#define MINILOG_DEBUG(...) (void) 0
#endif

#if MINILOG_ACTIVE_LEVEL <= MINILOG_LEVEL_INFO
#define MINILOG_LOGGER_INFO(logger, ...) MINILOG_LOGGER_CALL(logger, minilog::Level::kInfo, __VA_ARGS__)
#define MINILOG_INFO(...) MINILOG_LOGGER_INFO(minilog::defaultLogger(), __VA_ARGS__)
#else
#define MINILOG_LOGGER_INFO(logger, ...) (void) 0
#define MINILOG_INFO(...) (void) 0
#endif

#if MINILOG_ACTIVE_LEVEL <= MINILOG_LEVEL_WARN
#define MINILOG_LOGGER_WARN(logger, ...) MINILOG_LOGGER_CALL(logger, minilog::Level::kWarn, __VA_ARGS__)
#define MINILOG_WARN(...) MINILOG_LOGGER_WARN(minilog::defaultLogger(), __VA_ARGS__)
#else
#define MINILOG_LOGGER_WARN(logger, ...) (void) 0
#define MINILOG_WARN(...) (void) 0
#endif

#if MINILOG_ACTIVE_LEVEL <= MINILOG_LEVEL_ERROR
#define MINILOG_LOGGER_ERROR(logger, ...) MINILOG_LOGGER_CALL(logger, minilog::Level::kError, __VA_ARGS__)
#define MINILOG_ERROR(...) MINILOG_LOGGER_ERROR(minilog::defaultLogger(), __VA_ARGS__)
#else
#define MINILOG_LOGGER_ERROR(logger, ...) (void) 0
#define MINILOG_ERROR(...) (void) 0
#endif

#if MINILOG_ACTIVE_LEVEL <= MINILOG_LEVEL_CRITICAL
#define MINILOG_LOGGER_CRITICAL(logger, ...) MINILOG_LOGGER_CALL(logger, minilog::Level::kCritical, __VA_ARGS__)
#define MINILOG_CRITICAL(...) MINILOG_LOGGER_CRITICAL(minilog::defaultLogger(), __VA_ARGS__)
#else
#define MINILOG_LOGGER_CRITICAL(logger, ...) (void) 0
#define MINILOG_CRITICAL(...) (void) 0
#endif

#endif // MINILOG_LOG_H
