cmake_minimum_required(VERSION 3.20)
project(lark_framework LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(DAG_BUILD_EXAMPLES "Build DAG examples" ON)
option(DAG_BUILD_TESTS "Build DAG tests" ON)
option(DAG_ENABLE_ASAN "Enable Address/Leak sanitizer" OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

if(DAG_ENABLE_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---- core library ----
add_library(lark
  src/default_context.cpp
  src/node.cpp
  src/node_registry.cpp
  src/graph.cpp
  src/graph_builder.cpp
  src/executor.cpp
  src/coro/async_event.cpp
  src/coro/thread_pool.cpp
)
target_include_directories(lark PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
target_link_libraries(lark PUBLIC Threads::Threads)

# GCC needs an explicit flag for coroutines; clang enables them with -std=c++20.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(lark PUBLIC -fcoroutines)
endif()

if(DAG_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(DAG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
