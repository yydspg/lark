# ─────────────────────────────────────────────────────────────────────────────
# coro — standalone coroutine primitives.
#
# Depends on nothing but the standard library. Provides the thread-pool /
# event / task machinery used by the DAG executor and the column engine.
# ─────────────────────────────────────────────────────────────────────────────
add_library(lark_coro
  src/async_event.cpp
  src/thread_pool.cpp
)
target_include_directories(lark_coro PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lark_coro PUBLIC lark_metric Threads::Threads)
