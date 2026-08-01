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
option(DAG_ENABLE_AVX2 "Enable AVX2 SIMD (x86_64 only)" OFF)
option(DAG_ENABLE_AVX512 "Enable AVX-512 SIMD (x86_64 only)" OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

if(DAG_ENABLE_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---- core library ----
add_library(lark
  # DAG framework
  src/default_context.cpp
  src/node.cpp
  src/node_registry.cpp
  src/graph.cpp
  src/graph_builder.cpp
  src/executor.cpp
  src/coro/async_event.cpp
  src/coro/thread_pool.cpp
  # Column engine — Layer 2: compute kernels (SIMD-optimized)
  src/column/tensor.cpp
  src/column/tensor_table.cpp
  src/column/compute/add_tensor.cpp
  src/column/compute/sub_tensor.cpp
  src/column/compute/mul_tensor.cpp
  src/column/compute/div_tensor.cpp
  src/column/compute/scalar_tensor.cpp
  src/column/compute/reduce_tensor.cpp
  src/column/compute/compare_tensor.cpp
  src/column/compute/unary_tensor.cpp
  # Column engine — Layer 1: business nodes
  src/column/node/transform_node.cpp
  src/column/node/filter_node.cpp
  src/column/node/aggregate_node.cpp
  # Column engine — Graph pipeline
  src/column/graph.cpp
)
target_include_directories(lark PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
target_link_libraries(lark PUBLIC Threads::Threads)

# SIMD optimization for column engine compute kernels
# Platform detection:
#   ARM64 (Apple Silicon / Linux): NEON is always available — no extra flags
#   x86_64: SSE2 is default; enable AVX2/AVX-512 via CMake options
set(COLUMN_COMPUTE_SRCS
  src/column/compute/add_tensor.cpp
  src/column/compute/sub_tensor.cpp
  src/column/compute/mul_tensor.cpp
  src/column/compute/div_tensor.cpp
  src/column/compute/scalar_tensor.cpp
  src/column/compute/reduce_tensor.cpp
  src/column/compute/compare_tensor.cpp
  src/column/compute/unary_tensor.cpp
)

# Apply -O3 to all compute kernels
set_source_files_properties(${COLUMN_COMPUTE_SRCS} PROPERTIES COMPILE_OPTIONS "-O3")

# x86_64 SIMD flags
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
  if(DAG_ENABLE_AVX512)
    message(STATUS "Column engine SIMD: AVX-512 (x86_64)")
    set_source_files_properties(${COLUMN_COMPUTE_SRCS} PROPERTIES COMPILE_OPTIONS "-O3;-mavx512f")
  elseif(DAG_ENABLE_AVX2)
    message(STATUS "Column engine SIMD: AVX2+FMA (x86_64)")
    set_source_files_properties(${COLUMN_COMPUTE_SRCS} PROPERTIES COMPILE_OPTIONS "-O3;-mavx2;-mfma")
  else()
    message(STATUS "Column engine SIMD: SSE2 (x86_64 default)")
    # SSE2 is default on x86_64, no extra flags needed
  endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
  message(STATUS "Column engine SIMD: NEON (ARM64, always available)")
  # NEON is always available on ARM64, no extra flags needed
endif()

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
