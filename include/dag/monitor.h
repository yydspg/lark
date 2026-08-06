#pragma once

#include <chrono>
#include <exception>

namespace lark {

using std::chrono::nanoseconds;
using std::exception_ptr;

class Node;

// Observability hook invoked by the Executor around every node execution.
// Business code supplies an implementation to export metrics / tracing / logs.
// All callbacks may run concurrently from multiple worker threads, so
// implementations must be thread-safe.
class Monitor {
 public:
  virtual ~Monitor() = default;

  virtual void OnNodeStart(const Node& /*node*/) {}
  virtual void OnNodeSuccess(const Node& /*node*/,
                             std::chrono::nanoseconds /*elapsed*/) {}
  virtual void OnNodeFailure(const Node& /*node*/, std::exception_ptr /*error*/,
                             std::chrono::nanoseconds /*elapsed*/) {}
  virtual void OnNodeFallback(const Node& /*node*/) {}
  // Invoked for batch-disabled nodes (see Executor::Execute disabled_ids).
  virtual void OnNodeSkipped(const Node& /*node*/) {}
};

}  // namespace lark
