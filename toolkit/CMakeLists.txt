# ─────────────────────────────────────────────────────────────────────────────
# toolkit — the home for generic, reusable utilities (string helpers, Result,
# RAII scope guard, hashing, time/duration helpers).
#
# Zero dependencies. Add new shared helpers here instead of scattering copies
# across modules.
# ─────────────────────────────────────────────────────────────────────────────
add_library(lark_toolkit
  src/toolkit.cpp
)
target_include_directories(lark_toolkit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
