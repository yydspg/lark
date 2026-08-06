// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Cheap, deterministic non-cryptographic hashing.

#include <cstdint>
#include <string_view>

namespace lark::toolkit::hash {

// FNV-1a 64-bit. Well-distributed for small strings / keys.
constexpr uint64_t Fnv1a(std::string_view s) {
  uint64_t h = 14695981039346656037ULL;  // offset basis
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;  // FNV prime
  }
  return h;
}

// djb2 64-bit (x33). Simpler, slightly worse distribution.
constexpr uint64_t Djb2(std::string_view s) {
  uint64_t h = 5381;
  for (unsigned char c : s) h = h * 33 + c;
  return h;
}

// Combine two hashes (boost-style). Use to hash tuple-like keys.
inline uint64_t HashCombine(uint64_t seed, uint64_t value) {
  return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

}  // namespace lark::toolkit::hash
