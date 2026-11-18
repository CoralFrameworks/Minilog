# ==================================================================================================
# Usage examples
#
# Every example is a standalone executable. They are built when MINILOG_BUILD_DEMOS is on, which is the
# default when minilog is the top-level CMake project.
# ==================================================================================================

function(minilog_add_demo demo_name)
  add_executable(minilog_demo_${demo_name} ${demo_name}.cpp)

  target_link_libraries(minilog_demo_${demo_name} PRIVATE minilog)

  set_target_properties(minilog_demo_${demo_name} PROPERTIES FOLDER Examples)
endfunction()

minilog_add_demo(quickstart)
minilog_add_demo(file_logging)
minilog_add_demo(custom_sinks)
minilog_add_demo(backtrace)
