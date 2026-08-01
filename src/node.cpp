#include "dag/node.h"

namespace lark {

using std::string;
using std::move;

// ---------------------------------------------------------------------------
// NodeStatus
// ---------------------------------------------------------------------------
const char* ToString(NodeStatus status) noexcept {
  switch (status) {
    case NodeStatus::kPending:
      return "pending";
    case NodeStatus::kRunning:
      return "running";
    case NodeStatus::kSuccess:
      return "success";
    case NodeStatus::kFailed:
      return "failed";
    case NodeStatus::kFallback:
      return "fallback";
  }
  return "unknown";
}

// ---------------------------------------------------------------------------
// Node lifecycle
// ---------------------------------------------------------------------------
Node::Node() : status_(NodeStatus::kPending) {}

Node::~Node() = default;

// ---------------------------------------------------------------------------
// Business extension points (default implementations)
// ---------------------------------------------------------------------------
void Node::Compute(IContext& /*ctx*/) {}

Task<void> Node::Run(IContext& ctx) {
  Compute(ctx);
  co_return;
}

bool Node::Fallback(IContext& /*ctx*/, exception_ptr /*error*/) {
  return false;
}

// ---------------------------------------------------------------------------
// Framework-managed accessors
// ---------------------------------------------------------------------------
const string& Node::id() const noexcept { return id_; }

const string& Node::type() const noexcept { return type_; }

const std::vector<Node*>& Node::dependencies() const noexcept {
  return dependencies_;
}

NodeStatus Node::status() const noexcept {
  return status_.load(std::memory_order_acquire);
}

nanoseconds Node::elapsed() const noexcept { return elapsed_; }

exception_ptr Node::error() const noexcept { return error_; }

// ---------------------------------------------------------------------------
// Framework internals
// ---------------------------------------------------------------------------
void Node::SetIdentity(string id, string type) {
  id_ = move(id);
  type_ = move(type);
}

void Node::AddDependency(Node* dep) { dependencies_.push_back(dep); }

AsyncEvent& Node::done_event() noexcept { return done_event_; }

void Node::SetStatus(NodeStatus status) {
  status_.store(status, std::memory_order_release);
}

void Node::SetElapsed(nanoseconds elapsed) { elapsed_ = elapsed; }

void Node::SetError(exception_ptr error) { error_ = move(error); }

void Node::ResetRunState() {
  status_.store(NodeStatus::kPending, std::memory_order_release);
  elapsed_ = nanoseconds::zero();
  error_ = nullptr;
  done_event_.Reset();
}

}  // namespace lark
