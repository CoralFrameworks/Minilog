# minilog examples

Four small programs that show the intended use of the library. They are built when `MINILOG_BUILD_DEMOS`
is on, which is the default when minilog is the top-level CMake project.

## Build and run

From the root of the repository:

```sh
cmake -S . -B build -DMINILOG_BUILD_DEMOS=ON
cmake --build build --target minilog_demo_quickstart minilog_demo_file_logging \
  minilog_demo_custom_sinks minilog_demo_backtrace
```

Then run an example from the build directory (or from any directory, if you prefer to create the log files
there):

```sh
./build/demo/minilog_demo_quickstart        # Windows: build\demo\minilog_demo_quickstart.exe
./build/demo/minilog_demo_file_logging
./build/demo/minilog_demo_custom_sinks
./build/demo/minilog_demo_backtrace
```

With a multi-configuration generator (Visual Studio, Xcode) the executables are in
`build/demo/<Configuration>/` and the build command needs `--config <Configuration>`.

When minilog is a subdirectory of a parent project, enable the examples there instead:

```cmake
set(MINILOG_BUILD_DEMOS ON CACHE BOOL "" FORCE)
add_subdirectory(libs/minilog)
```

## What each example shows

| Executable | Shows |
| --- | --- |
| `minilog_demo_quickstart` | `ScopedLogging`, a console-only `LoggingConfig`, a pattern with time/level/source, the log macros and the free functions. |
| `minilog_demo_file_logging` | A console sink and a rotating file sink in one configuration, a different pattern and level per sink, `flushLevel`, and the files that are left on disk. Creates `demo_logs/` in the current working directory. |
| `minilog_demo_custom_sinks` | A hand-built `Logger` with `DistSink`: an `OStreamSink` for the console, a `RingBufferSink` with the last five messages, and a `CallbackSink` that counts errors. |
| `minilog_demo_backtrace` | `Logger::enableBacktrace()`: messages below the logger level are kept in a ring buffer and printed by `dumpBacktrace()` when an error happens. |

## Notes

* A Debug build compiles in `MINILOG_TRACE` and `MINILOG_DEBUG`; a release build keeps debug and above.
  `MINILOG_ACTIVE_LEVEL` in the root `CMakeLists.txt` is the compile-time switch, while
  `minilog::setLevel()` / `config.level` is the runtime filter.
* Every example ends by destroying its `ScopedLogging` or its local logger, which flushes and closes the
  files it opened. `minilog::shutdown()` is the explicit version of the same operation.
* The examples write to `stdout`/`stderr` and, for the file example, below `demo_logs/`. Remove that
  directory when you are done.
