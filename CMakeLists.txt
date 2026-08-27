cmake_minimum_required(VERSION 3.20)
project(rill CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(RILL_GC_STRESS     "Collect before every allocation" OFF)
option(RILL_NANBOX        "Pack values into 8 bytes"        OFF)
option(RILL_COMPUTED_GOTO "Use computed-goto dispatch"      OFF)
option(RILL_FOLD          "Constant-fold at emit time"      ON)
option(RILL_SUPEROPS      "Specialized opcodes"             ON)
option(RILL_TRACE         "Trace execution"                 OFF)

add_library(rill_core
  src/common.cpp
)
target_include_directories(rill_core PUBLIC src)
target_compile_options(rill_core PUBLIC -Wall -Wextra -Werror)

foreach(opt RILL_GC_STRESS RILL_NANBOX RILL_COMPUTED_GOTO RILL_FOLD RILL_SUPEROPS RILL_TRACE)
  if(${opt})
    target_compile_definitions(rill_core PUBLIC ${opt}=1)
  endif()
endforeach()

add_executable(rill src/main.cpp)
target_link_libraries(rill PRIVATE rill_core)

enable_testing()

add_executable(rill_tests
  tests/unit/main.cpp
)
target_link_libraries(rill_tests PRIVATE rill_core)
target_include_directories(rill_tests PRIVATE tests/unit)
add_test(NAME unit COMMAND rill_tests)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME behavior
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tests/run_behavior_tests.py
          $<TARGET_FILE:rill>
          ${CMAKE_SOURCE_DIR}/tests/lang)
