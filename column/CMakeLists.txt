# ─────────────────────────────────────────────────────────────────────────────
# column — feed / compute / fetch column engine (business + execution layers).
#
# Depends on the coroutine module (lark_coro) for async graph execution; it does
# NOT depend on the dag module. Backends, op kernels and the Pipeline compiler
# live here as separate source families to keep classes small.
# ─────────────────────────────────────────────────────────────────────────────
add_library(lark_column
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
  # Column engine v4 — feed/compute/fetch engine
  src/column/tensor_store.cpp
  src/column/monitor.cpp
  src/column/feed_fetch.cpp
  src/column/context/execution_context.cpp
  src/column/exec/tensor_op.cpp
  src/column/exec/ops.cpp
  src/column/exec/ops_binary.cpp
  src/column/exec/ops_unary.cpp
  src/column/exec/ops_reduce.cpp
  src/column/exec/ops_compare.cpp
  src/column/exec/ops_special.cpp
  src/column/exec/exec_node.cpp
  src/column/exec/compute_graph.cpp
  src/column/backend/backend.cpp
  src/column/biz/module.cpp
  src/column/biz/dsl.cpp
  src/column/biz/pipeline.cpp
  src/column/biz/pipeline_compiler.cpp
)
target_include_directories(lark_column PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lark_column PUBLIC lark_coro lark_monitor)

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
  # execution-layer op kernels
  src/column/exec/ops_binary.cpp
  src/column/exec/ops_unary.cpp
  src/column/exec/ops_reduce.cpp
  src/column/exec/ops_compare.cpp
  src/column/exec/ops_special.cpp
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
