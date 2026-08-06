# ─────────────────────────────────────────────────────────────────────────────
# metric — unified, pluggable monitoring abstraction + built-in observability.
#
# Every LARK module (dag / column / rpc / coro / cache) reports through the
# generic lark::metric::Monitor interface; business code selects the concrete
# implementation via MonitorFactory ("null" / "logging" / "stats" / "composite").
#
# Also ships flame-graph style CPU profiling (profile.h) and null-pointer /
# invariant / anomaly probes (probe.h) on top of the same abstraction.
# Depends on the toolkit subproject (string / time helpers).
# ─────────────────────────────────────────────────────────────────────────────
add_library(lark_metric
  src/metric.cpp
  src/profile.cpp
  src/probe.cpp
)
target_include_directories(lark_metric PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lark_metric PUBLIC lark_toolkit)
