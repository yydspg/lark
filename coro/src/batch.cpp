// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "coro/batch.h"

#include <algorithm>

namespace lark::coro::batch {

std::size_t DefaultBatchSize(std::size_t total, std::size_t workers) {
  if (workers == 0) workers = 1;
  if (total <= workers) return 1;
  return (total + workers - 1) / workers;
}

}  // namespace lark::coro::batch
