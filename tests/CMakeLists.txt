# ==================================================================================================
# Tests
#
# Each test is a standalone executable named tests_<Thing>, registered with CTest. The working directory
# is pinned to the binary directory of the test, so a test that writes files behaves the same when it is
# started directly and when it is started by ctest.
# ==================================================================================================

function(minilog_add_test test_target)
  add_executable(${test_target} ${test_target}.cpp)

  target_link_libraries(${test_target} PRIVATE minilog)

  set_target_properties(${test_target} PROPERTIES FOLDER Tests)

  add_test(NAME ${test_target} COMMAND ${test_target})

  # The suites write below a relative directory of their own; pinning the working directory makes that
  # location independent of how the test was launched.
  set_tests_properties(${test_target} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()

minilog_add_test(tests_MiniLogSinks)
minilog_add_test(tests_MiniLogCore)
