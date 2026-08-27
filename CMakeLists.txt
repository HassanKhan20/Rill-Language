cmake_minimum_required(VERSION 3.20)
project(rill CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(RILL_GC_STRESS     "Collect before every allocation" OFF)
option(RILL_FOLD          "Constant-fold at emit time"      ON)
option(RILL_SUPEROPS      "Specialized opcodes"             ON)
option(RILL_TRACE         "Trace execution"                 OFF)

add_library(rill_core
  src/common.cpp
  src/lexer.cpp
  src/value.cpp
  src/chunk.cpp
  src/debug.cpp
  src/table.cpp
  src/object.cpp
  src/compiler.cpp
  src/parser.cpp
  src/vm.cpp
  src/builtins.cpp
  src/gc.cpp
)
target_include_directories(rill_core PUBLIC src)
target_compile_options(rill_core PUBLIC -Wall -Wextra -Werror)

foreach(opt RILL_GC_STRESS RILL_FOLD RILL_SUPEROPS RILL_TRACE)
  if(${opt})
    target_compile_definitions(rill_core PUBLIC ${opt}=1)
  endif()
endforeach()

add_executable(rill src/main.cpp)
target_link_libraries(rill PRIVATE rill_core)

enable_testing()

add_executable(rill_tests
  tests/unit/main.cpp
  tests/unit/test_lexer.cpp
  tests/unit/test_chunk.cpp
  tests/unit/test_table.cpp
  tests/unit/test_object.cpp
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
