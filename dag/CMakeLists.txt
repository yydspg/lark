# ─────────────────────────────────────────────────────────────────────────────
# dag — DAG business execution framework.
#
# Depends on the coroutine module (lark_coro). Provides Node / Graph /
# GraphBuilder / NodeRegistry / Executor / Monitor / StatsCollector.
# ─────────────────────────────────────────────────────────────────────────────
add_library(lark_dag
  src/node.cpp
  src/node_registry.cpp
  src/graph.cpp
  src/graph_builder.cpp
  src/executor.cpp
  src/stats.cpp
  src/default_context.cpp
)
target_include_directories(lark_dag PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lark_dag PUBLIC lark_coro lark_monitor)
