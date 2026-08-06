# ─────────────────────────────────────────────────────────────────────────────
# cache — highly-abstract cache module.
#
# Business code programs against the Cache interface; implementations are
# selected via CacheFactory:
#   * "local"  — thread-safe in-memory TTL cache with LRU eviction
#   * "remote" — RPC-backed cache (CacheRpcService served via lark_rpc)
# Emits cache.* events into the configured lark::metric::Monitor.
# Depends on the monitor and rpc subprojects.
# ─────────────────────────────────────────────────────────────────────────────
add_library(lark_cache
  src/cache.cpp
)
target_include_directories(lark_cache PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lark_cache PUBLIC lark_metric lark_rpc)
