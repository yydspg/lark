# ─────────────────────────────────────────────────────────────────────────────
# rpc — generic RPC framework (OOP interfaces + backend factory).
#
# Transport-agnostic (in-process / gRPC / brpc); depends only on the standard
# library.
# ─────────────────────────────────────────────────────────────────────────────
add_library(lark_rpc
  src/rpc/backend.cpp
  src/rpc/inproc_backend.cpp
  src/rpc/grpc_backend.cpp
  src/rpc/brpc_backend.cpp
)
target_include_directories(lark_rpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lark_rpc PUBLIC lark_metric Threads::Threads)
