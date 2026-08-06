// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Business-oriented DAG operations + aspects (切面).
//
// Business code implements lark::dag::Op and only writes business logic:
//   class FetchUser : public dag::Op {
//    public:
//     const std::string& Name() const noexcept override { static const std::string n = "fetch_user"; return n; }
//     coro::Task<void> Execute(dag::Context& data) override {
//       auto id = data.Get<int>("user_id");
//       data.Set("user", co_await LoadUser(id));
//     }
//   };
//
// Cross-cutting behavior (condition-based auto-skip, tracing, timing) lives in
// OpAspect implementations applied around every op — an aspect, not logic
// baked into the op.

#include <string>

#include "coro/context.h"
#include "coro/task.h"

namespace lark::dag {

// The lock-free data bag shared by all ops of a run. No locking: sequential
// ops never overlap, and concurrent (independent) ops must write distinct
// fields — a business convention, so there is never contention.
using Context = lark::coro::Context;

// Execution status of an op within one graph run.
enum class OpStatus { kPending, kRunning, kSuccess, kSkipped, kFailed };

inline const char* ToString(OpStatus status) noexcept {
  switch (status) {
    case OpStatus::kPending:
      return "pending";
    case OpStatus::kRunning:
      return "running";
    case OpStatus::kSuccess:
      return "success";
    case OpStatus::kSkipped:
      return "skipped";
    case OpStatus::kFailed:
      return "failed";
  }
  return "unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// Op: the business-facing unit of computation.
//
// Business subclasses override Name() and Execute() only — the framework
// schedules the coroutine, waits for dependencies, applies aspects and times
// the op. No framework details leak into business code.
// ─────────────────────────────────────────────────────────────────────────────
class Op {
 public:
  virtual ~Op() = default;

  virtual const std::string& Name() const noexcept = 0;

  // Async business logic. Reads inputs from / writes outputs onto the shared
  // Context. Runs on the dedicated dag thread pool.
  virtual coro::Task<void> Execute(Context& data) = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// OpAspect (切面): cross-cutting hooks invoked by the executor around every op.
//
//   ShouldSkip — return true to automatically skip this op for this run
//                (condition-based skip; the op itself stays untouched).
//   OnBefore   — run before the op executes.
//   OnAfter    — run after the op completes, with its final status.
// ─────────────────────────────────────────────────────────────────────────────
class OpAspect {
 public:
  virtual ~OpAspect() = default;

  virtual bool ShouldSkip(const Op& op, const Context& data) {
    (void)op;
    (void)data;
    return false;
  }
  virtual void OnBefore(const Op& op, Context& data) {
    (void)op;
    (void)data;
  }
  virtual void OnAfter(const Op& op, Context& data, OpStatus status) {
    (void)op;
    (void)data;
    (void)status;
  }
};

}  // namespace lark::dag
