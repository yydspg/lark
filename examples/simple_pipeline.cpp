// End-to-end example: a request arrives, we build its Context, construct a DAG
// from key/value NodeDefs, and execute it fully asynchronously.
//
// Graph shape (arrows = "depends on"):
//
//     fetch_user  ─┐
//                  ├─> build_features ─> rank
//     fetch_orders ┘                     ^
//                                         │
//     risk_signal (flaky, falls back) ────┘
//
// build_features has two inputs (fetch_user, fetch_orders) that run in
// parallel; risk_signal runs in parallel with the user/orders branch.

#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dag/dag.h"

namespace {

using namespace std::chrono_literals;

// ---- business domain contexts (injected into Context) --------------------
struct RequestDomain {
  std::string request_id;
  std::int64_t user_id = 0;
};

// ---- business data passed between nodes ----------------------------------
struct UserProfile {
  std::string name;
  int level = 0;
};

struct Orders {
  std::vector<std::string> ids;
};

struct Features {
  double score = 0.0;
};

// ---- business nodes (subclass Node, implement Compute) -------------------
// Each node is automatically registered at startup using LARK_NODE macro!
// No manual registration calls needed anywhere.

class FetchUserNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    const auto& req = lark::RequireDomain<RequestDomain>(ctx);
    std::this_thread::sleep_for(40ms);  // simulate I/O
    lark::Set(ctx, "user", UserProfile{"user-" + req.request_id, 3});
  }
};
LARK_NODE("fetch_user", FetchUserNode);  // Auto-registered!

class FetchOrdersNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    std::this_thread::sleep_for(40ms);  // runs in parallel with FetchUser
    lark::Set(ctx, "orders", Orders{{"o1", "o2", "o3"}});
  }
};
LARK_NODE("fetch_orders", FetchOrdersNode);  // Auto-registered!

// A flaky external dependency that fails but degrades gracefully.
class RiskSignalNode : public lark::Node {
  void Compute(lark::IContext& /*ctx*/) override {
    throw std::runtime_error("risk service timeout");
  }
  bool Fallback(lark::IContext& ctx, std::exception_ptr /*error*/) override {
    lark::Set(ctx, "risk", 0.5);  // safe default
    return true;                 // degraded but usable
  }
};
LARK_NODE("risk_signal", RiskSignalNode);  // Auto-registered!

class BuildFeaturesNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    auto user = lark::Get<UserProfile>(ctx, "user");
    auto orders = lark::Get<Orders>(ctx, "orders");
    Features f;
    f.score = (user ? user->level : 0) * 10.0 +
              (orders ? static_cast<double>(orders->ids.size()) : 0.0);
    lark::Set(ctx, "features", f);
  }
};
LARK_NODE("build_features", BuildFeaturesNode);  // Auto-registered!

class RankNode : public lark::Node {
  void Compute(lark::IContext& ctx) override {
    auto features = lark::Get<Features>(ctx, "features");
    auto risk = lark::Get<double>(ctx, "risk");
    const double final_score =
        (features ? features->score : 0.0) * (risk ? *risk : 1.0);
    lark::Set(ctx, "final_score", final_score);
  }
};
LARK_NODE("rank", RankNode);  // Auto-registered!

// A monitor that logs each node's outcome (thread-safe).
class ConsoleMonitor : public lark::Monitor {
 public:
  void OnNodeSuccess(const lark::Node& node,
                     std::chrono::nanoseconds elapsed) override {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "  [success] " << node.id() << " ("
              << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                     .count()
              << " ms)\n";
  }

  void OnNodeFailure(const lark::Node& node, std::exception_ptr /*error*/,
                     std::chrono::nanoseconds elapsed) override {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "  [FAILED] " << node.id() << " ("
              << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                     .count()
              << " ms)\n";
  }

  void OnNodeFallback(const lark::Node& node) override {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "  [FALLBACK] " << node.id() << "\n";
  }

 private:
  std::mutex mutex_;
};

}  // namespace

int main() {
  std::cout << "Executing DAG on " << lark::NodeRegistry::Instance()
                   .RegisteredTypes().size()
              << " auto-registered node types...\n\n";

  // Build the graph from key/value definitions.
  // The framework automatically discovers all nodes registered via LARK_NODE.
  lark::GraphBuilder builder;
  auto graph = builder.Build({
      {"fetch_user", {}, "fetch_user"},
      {"fetch_orders", {}, "fetch_orders"},
      {"risk_signal", {}, "risk_signal"},
      {"build_features", {"fetch_user", "fetch_orders"}, "build_features"},
      {"rank", {"build_features", "risk_signal"}, "rank"},
  });

  // Inject the business domain context.
  lark::DefaultContext ctx;
  lark::ProvideDomain<RequestDomain>(ctx, lark::string{"req-123"}, 42LL);

  // Execute the graph fully asynchronously.
  lark::Executor executor;
  executor.SetMonitor(std::make_shared<ConsoleMonitor>());

  std::cout << "Executing DAG on " << executor.compute_worker_count()
            << " compute + " << executor.io_worker_count() << " io + "
            << executor.background_worker_count()
            << " background workers...\n";
  const auto start = std::chrono::steady_clock::now();
  executor.Execute(*graph, ctx);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  // Read the final result.
  if (auto score = lark::Get<double>(ctx, "final_score")) {
    std::cout << "\nfinal_score = " << *score << "  (wall time "
              << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                     .count()
              << " ms)\n";
  }

  if (auto rank_node = graph->Find("rank")) {
    std::cout << "rank status = " << lark::ToString(rank_node->status())
              << "\n";
  }

  return 0;
}
