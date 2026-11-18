cmake_minimum_required(VERSION 3.19)
project(minilog)

set(TARGET minilog)
set(PUBLIC_HDR_DIR include)

# ==================================================================================================
# Project flags
# ==================================================================================================
# MINILOG_ACTIVE_LEVEL is the level the compile time filters of the log macros use: every call below it is
# compiled out. A debug build keeps every level including trace, a release build keeps debug and above - the
# runtime level of the logger decides what is actually written. The generator expression makes the definition
# follow the configuration that is being built, which is also what the Visual Studio generator needs, and it is
# PUBLIC so that the sources of the project that includes minilog agree with the library on the level.
#
# A single definition on the target replaces the directory wide add_definitions() of the previous revision: the
# two of them defined the macro twice on the command line, which MSVC reports as "overriding '/D' option".

# ==================================================================================================
# General compiler flags
# ==================================================================================================
# CMAKE_CXX_STANDARD is what adds /std:c++20 to MSVC and -std=c++20 to GCC/Clang. A plain
# CXX_STANDARD variable is local to this file and never reaches the compiler.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==================================================================================================
# Options
# ==================================================================================================
# The tests and the examples are built by default when minilog is the top-level project. A parent
# project - the Coral framework - builds the tests when it exports IS_HOST_PLATFORM (the framework's
# own test switch) or when it sets MINILOG_BUILD_TESTS explicitly; the examples stay off unless asked for.
if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(MINILOG_TOP_LEVEL ON)
else ()
  set(MINILOG_TOP_LEVEL OFF)
endif ()

if (MINILOG_TOP_LEVEL OR IS_HOST_PLATFORM)
  set(MINILOG_TEST_DEFAULT ON)
else ()
  set(MINILOG_TEST_DEFAULT OFF)
endif ()

option(MINILOG_BUILD_TESTS "build the minilog test suites" ${MINILOG_TEST_DEFAULT})
option(MINILOG_BUILD_DEMOS "build the minilog usage examples in demo/" ${MINILOG_TOP_LEVEL})

option(MINILOG_NO_THREAD_ID "prevent minilog from querying the thread id on each log call if thread id is not needed" OFF)
if (MINILOG_NO_THREAD_ID)
  add_definitions(-DMINILOG_NO_THREAD_ID)
endif ()

option(MINILOG_NO_TLS "prevent minilog from using thread local storage" OFF)
if (MINILOG_NO_TLS)
  add_definitions(-DMINILOG_NO_TLS)
endif ()

# ==================================================================================================
# Sources and headers
# ==================================================================================================

set(PUBLIC_HDRS
    include/minilog/sinks/AnsiColorSink.h
    include/minilog/sinks/BaseSink.h
    include/minilog/sinks/CallbackSink.h
    include/minilog/sinks/DailyFileSink.h
    include/minilog/sinks/DebuggerSink.h
    include/minilog/sinks/DistSink.h
    include/minilog/sinks/FileSink.h
    include/minilog/sinks/NullSink.h
    include/minilog/sinks/OStreamSink.h
    include/minilog/sinks/RingBufferSink.h
    include/minilog/sinks/RotatingFileSink.h

    include/minilog/Formatter.h
    include/minilog/log.h
    include/minilog/Logger.h
    include/minilog/LogSetup.h
    include/minilog/PatternFormatter.h
    include/minilog/Sink.h
)

set(SRCS
    src/BackTracer.cpp
    src/common.cpp
    src/FileHelper.cpp
    src/log.cpp
    src/Logger.cpp
    src/LogSetup.cpp
    src/Message.cpp
    src/MessageBuffer.cpp
    src/PatternFormatter.cpp
    src/Utils.cpp
)

set(PRIVATE_HDRS
    include/private/minilog/BackTracer.h
    include/private/minilog/FileHelper.h
    include/private/minilog/FmtHelper.h
    include/private/minilog/mdc.h
    include/private/minilog/Message.h
    include/private/minilog/MessageQueue.h
    include/private/minilog/mutex.h
    include/private/minilog/noncopyable.h
    include/private/minilog/Utils.h
)

if (ANDROID)
  list(APPEND PUBLIC_HDRS
    include/minilog/sinks/AndroidSink.h
  )
endif ()

if (WIN32)
  list(APPEND PUBLIC_HDRS
    include/minilog/sinks/MsvcSink.h
    include/minilog/sinks/WinColorSink.h
  )
endif ()

# ==================================================================================================
# Includes & target definition
# ==================================================================================================
include_directories(${PUBLIC_HDR_DIR})

add_library(${TARGET} STATIC ${PRIVATE_HDRS} ${PUBLIC_HDRS} ${SRCS})

target_include_directories(${TARGET} PUBLIC ${PUBLIC_HDR_DIR})
set_target_properties(${TARGET} PROPERTIES FOLDER Libs)

target_compile_definitions(${TARGET} PUBLIC
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:MINILOG_ACTIVE_LEVEL=0>
    $<$<NOT:$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>>:MINILOG_ACTIVE_LEVEL=1>)

if (ANDROID)
  target_link_libraries(${TARGET} PRIVATE log)
endif ()

# ==================================================================================================
# Installation
# ==================================================================================================
install(TARGETS ${TARGET} ARCHIVE DESTINATION lib/${DIST_DIR})
install(DIRECTORY ${PUBLIC_HDR_DIR}/${TARGET} DESTINATION include)
# The public headers include the private ones, so they are installed next to them.
install(DIRECTORY ${PUBLIC_HDR_DIR}/private DESTINATION include)

# ==================================================================================================
# Test and examples
# ==================================================================================================
if (MINILOG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif ()

if (MINILOG_BUILD_DEMOS)
  add_subdirectory(demo)
endif ()
