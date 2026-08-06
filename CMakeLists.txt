cmake_minimum_required(VERSION 3.20)
project(lark_framework LANGUAGES CXX)

# ─────────────────────────────────────────────────────────────────────────────
# LARK multi-module build (Maven/Gradle-style parent project).
#
# The framework is split into independent subprojects (each with its own
# CMakeLists.txt + include/ + src/), linked by a parent aggregate target:
#
#   coro    → lark_coro    standalone coroutine primitives (ThreadPool,
#                          AsyncEvent, Task, FireAndForget)
#   dag     → lark_dag     DAG business execution framework (depends on coro)
#   column  → lark_column  feed/compute/fetch column engine (depends on coro)
#   rpc     → lark_rpc     generic RPC framework (standalone)
#   monitor → lark_monitor unified pluggable monitoring (zero deps)
#   cache   → lark_cache   abstract cache (local / remote), depends on monitor+rpc
#   lark    ────────────── parent aggregate: links all subprojects
#
# Callers can either link the individual libraries (fine-grained) or the single
# `lark` target (aggregate).
# ─────────────────────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(DAG_BUILD_EXAMPLES "Build DAG examples" ON)
option(DAG_BUILD_TESTS "Build DAG tests" ON)
option(DAG_ENABLE_ASAN "Enable Address/Leak sanitizer" OFF)
option(DAG_ENABLE_AVX2 "Enable AVX2 SIMD (x86_64 only)" OFF)
option(DAG_ENABLE_AVX512 "Enable AVX-512 SIMD (x86_64 only)" OFF)
option(LARK_WITH_GRPC "Enable the gRPC RPC backend adapter" OFF)
option(LARK_WITH_BRPC "Enable the brpc RPC backend adapter" OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

if(DAG_ENABLE_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)

# Export compile_commands.json so IDE language servers (clangd / VSCode C++)
# get correct per-file flags (includes, -std=c++20). Editors should point at
# this file (or the root compile_commands.json symlink).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- subprojects (children) ----
add_subdirectory(coro)
add_subdirectory(dag)
add_subdirectory(column)
add_subdirectory(rpc)
add_subdirectory(monitor)
add_subdirectory(cache)

# ---- parent aggregate target ----
add_library(lark INTERFACE)
target_link_libraries(lark INTERFACE lark_coro lark_dag lark_column lark_rpc lark_monitor lark_cache)

# GCC needs an explicit flag for coroutines; clang enables them with -std=c++20.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  foreach(_mod lark_coro lark_dag lark_column lark_rpc lark_monitor lark_cache)
    target_compile_options(${_mod} PUBLIC -fcoroutines)
  endforeach()
endif()

if(DAG_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(DAG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
