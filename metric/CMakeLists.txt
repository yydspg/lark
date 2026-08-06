# ─────────────────────────────────────────────────────────────────────────────
# metric — unified, pluggable monitoring abstraction.
#
# Every LARK module (dag / column / rpc / coro / cache) reports through the
# generic lark::metric::Monitor interface; business code selects the concrete
# implementation via MonitorFactory ("null" / "logging" / "stats" / "composite").
# Zero dependencies.
# ─────────────────────────────────────────────────────────────────────────────
add_library(lark_metric
  src/metric.cpp
)
target_include_directories(lark_metric PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
