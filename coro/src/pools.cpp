// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "coro/pools.h"

#include <utility>

namespace lark::coro {

namespace {
ThreadPoolConfig WithKind(ThreadPoolConfig config, PoolKind kind) {
  config.kind = kind;
  return config;
}
}  // namespace

Pools::Pools(ThreadPoolConfig compute, ThreadPoolConfig io,
             ThreadPoolConfig lowload, ThreadPoolConfig dag,
             ThreadPoolConfig daemon) {
  pools_[IndexOf(PoolKind::kCompute)] =
      std::make_unique<ThreadPool>(WithKind(std::move(compute), PoolKind::kCompute));
  pools_[IndexOf(PoolKind::kIo)] =
      std::make_unique<ThreadPool>(WithKind(std::move(io), PoolKind::kIo));
  pools_[IndexOf(PoolKind::kLowLoad)] =
      std::make_unique<ThreadPool>(WithKind(std::move(lowload), PoolKind::kLowLoad));
  pools_[IndexOf(PoolKind::kDag)] =
      std::make_unique<ThreadPool>(WithKind(std::move(dag), PoolKind::kDag));
  pools_[IndexOf(PoolKind::kDaemon)] =
      std::make_unique<ThreadPool>(WithKind(std::move(daemon), PoolKind::kDaemon));
}

}  // namespace lark::coro
