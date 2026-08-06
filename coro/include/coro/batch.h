// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Batch-parallel processing over coroutines.
//
// For a long list (e.g. a goodsList) where several compute coroutines should
// cooperatively compute attributes or run complex field transforms: the list
// is split into batches and each batch runs as its own coroutine on a pool,
// so multiple coroutines work in parallel while results stay in the original
// order (distinct elements -> no locking needed by convention).
//
//   coro::batch::BatchMap(goods, /*batch_size=*/0, /*workers=*/8, pool,
//     [](Goods& g) {
//       g.discount   = Normalize(g.price);
//       g.finalPrice = g.discount * g.rate;
//     }).Wait();

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "coro/future.h"
#include "coro/task.h"
#include "coro/thread_pool.h"

namespace lark::coro::batch {

// Pick a batch size ~ total/workers (used when batch_size is left 0).
std::size_t DefaultBatchSize(std::size_t total, std::size_t workers);

namespace detail {

// Split [0, n) into batches; each batch runs as its own coroutine on `pool`.
// `batch_fn(begin, end, batch_index)` runs on a worker. The returned future
// completes when every batch finished.
template <typename BatchFn>
Future<void> RunBatches(std::size_t n, std::size_t batch_size,
                        std::size_t workers, ThreadPool& pool,
                        BatchFn batch_fn) {
  if (n == 0) return Future<void>::Just();
  if (batch_size == 0) batch_size = DefaultBatchSize(n, workers);
  if (batch_size == 0) batch_size = 1;
  const std::size_t batches = (n + batch_size - 1) / batch_size;

  std::vector<Future<void>> futures;
  futures.reserve(batches);
  for (std::size_t b = 0; b < batches; ++b) {
    const std::size_t begin = b * batch_size;
    const std::size_t end = std::min(begin + batch_size, n);
    futures.push_back(Future<void>::Async(
        [b, begin, end, &pool, batch_fn]() -> Task<void> {
          co_await pool.Schedule();
          batch_fn(begin, end, b);
          co_return;
        },
        pool));
  }
  return Future<void>::AllOf(futures);
}

}  // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// BatchMap: in-place transform of every element. fn(T&) runs once per element;
// batches execute concurrently on the pool. Completes when all are done.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, typename Fn>
Future<void> BatchMap(std::vector<T>& data, std::size_t batch_size,
                      std::size_t workers, ThreadPool& pool, Fn&& fn) {
  return detail::RunBatches(
      data.size(), batch_size, workers, pool,
      [&data, fn = std::forward<Fn>(fn)](std::size_t begin, std::size_t end,
                                         std::size_t) {
        for (std::size_t i = begin; i < end; ++i) fn(data[i]);
      });
}

// ─────────────────────────────────────────────────────────────────────────────
// BatchTransform: map into a new, order-preserving vector. fn(const T&) -> U.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, typename U, typename Fn>
Future<std::vector<U>> BatchTransform(const std::vector<T>& data,
                                      std::size_t batch_size,
                                      std::size_t workers, ThreadPool& pool,
                                      Fn&& fn) {
  const std::size_t n = data.size();
  auto out = std::make_shared<std::vector<U>>(n);
  if (n == 0) return Future<std::vector<U>>::Just(*out);

  auto runner = detail::RunBatches(
      n, batch_size, workers, pool,
      [&data, out, fn = std::forward<Fn>(fn)](std::size_t begin,
                                              std::size_t end, std::size_t) {
        for (std::size_t i = begin; i < end; ++i) (*out)[i] = fn(data[i]);
      });

  auto state = std::make_shared<::lark::coro::detail::FutureState<std::vector<U>>>();
  runner.WhenComplete([out, state](std::exception_ptr error) {
    if (error) {
      state->ResolveError(error);
    } else {
      state->ResolveValue(std::move(*out));
    }
  });
  return Future<std::vector<U>>::FromState(state);
}

// ─────────────────────────────────────────────────────────────────────────────
// BatchForEach: run fn(const T&) for every element (side effects elsewhere).
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, typename Fn>
Future<void> BatchForEach(const std::vector<T>& data, std::size_t batch_size,
                          std::size_t workers, ThreadPool& pool, Fn&& fn) {
  return detail::RunBatches(
      data.size(), batch_size, workers, pool,
      [&data, fn = std::forward<Fn>(fn)](std::size_t begin, std::size_t end,
                                         std::size_t) {
        for (std::size_t i = begin; i < end; ++i) fn(data[i]);
      });
}

// ─────────────────────────────────────────────────────────────────────────────
// BatchReduce: each batch folds its range into a partial accumulator via
// `fn(acc, element)`; partials are then combined in order with `combine`.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, typename Acc, typename Fn, typename Combine>
Future<Acc> BatchReduce(const std::vector<T>& data, std::size_t batch_size,
                        std::size_t workers, ThreadPool& pool, Acc init,
                        Fn&& fn, Combine&& combine) {
  const std::size_t n = data.size();
  if (n == 0) return Future<Acc>::Just(std::move(init));
  if (batch_size == 0) batch_size = DefaultBatchSize(n, workers);
  if (batch_size == 0) batch_size = 1;
  const std::size_t batches = (n + batch_size - 1) / batch_size;

  auto partials = std::make_shared<std::vector<Acc>>(batches);
  auto runner = detail::RunBatches(
      n, batch_size, workers, pool,
      [&data, partials, init, fn = std::forward<Fn>(fn)](
          std::size_t begin, std::size_t end, std::size_t batch) {
        Acc acc = init;
        for (std::size_t i = begin; i < end; ++i) acc = fn(acc, data[i]);
        (*partials)[batch] = acc;
      });

  auto state = std::make_shared<::lark::coro::detail::FutureState<Acc>>();
  runner.WhenComplete([partials, combine = std::forward<Combine>(combine),
                       init, state](std::exception_ptr error) {
    if (error) {
      state->ResolveError(error);
      return;
    }
    Acc total = init;
    for (const auto& p : *partials) total = combine(total, p);
    state->ResolveValue(std::move(total));
  });
  return Future<Acc>::FromState(state);
}

}  // namespace lark::coro::batch
