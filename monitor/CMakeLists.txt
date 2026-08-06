# ─────────────────────────────────────────────────────────────────────────────
# monitor — unified, pluggable monitoring abstraction.
#
# Every LARK module (dag / column / rpc / coro / cache) reports through the
# generic lark::monitor::Monitor interface; business code selects the concrete
# implementation via MonitorFactory ("null" / "logging" / "stats" / "composite").
# Zero dependencies.
# ─────────────────────────────────────────────────────────────────────────────
add_library(lark_monitor
  src/monitor.cpp
)
target_include_directories(lark_monitor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
