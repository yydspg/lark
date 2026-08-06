#pragma once

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "dag/graph.h"
#include "dag/node_registry.h"

namespace lark {

using std::initializer_list;
using std::string;
using std::vector;

// One key/value entry describing a node in a graph specification.
//
// Conceptually a kv pair: the "key" is the node (its registered `type`, and an
// optional distinct `id` when the same type appears multiple times); the
// "value" is the list of dependency ids that must complete before it runs.
//
//   NodeDef{"fetch_user"}                          // no deps, id == type
//   NodeDef{"aggregate", {"fetch_user", "orders"}} // deps by id
//   NodeDef{"score", {"aggregate"}, "score_v2"}    // explicit id
struct NodeDef {
  NodeDef(string type, vector<string> deps = {}, string id = {});

  string type;               // registered node type name (selects a node)
  vector<string> deps;       // dependency node ids
  string id;                 // unique id; defaults to `type` when empty
};

// Builds and validates a Graph from an array of key/value NodeDefs.
class GraphBuilder {
 public:
  // Defaults to the process-wide registry; pass a custom one for tests.
  explicit GraphBuilder(const NodeRegistry& registry = NodeRegistry::Instance());

  // Materialize a graph. Throws std::invalid_argument / std::runtime_error on:
  //   * unknown node type, * duplicate node id, * missing dependency id,
  //   * a cycle (the graph would not be a DAG).
  std::unique_ptr<Graph> Build(const vector<NodeDef>& defs) const;

  std::unique_ptr<Graph> Build(initializer_list<NodeDef> defs) const;

 private:
  // Pipeline stages of Build(). Each is a pure validation / wiring pass so
  // that failure modes are reported in a deterministic order and the whole
  // builder stays readable.
  void CreateAndIndexNodes(const vector<NodeDef>& defs, Graph& graph) const;
  void WireDependencies(const vector<NodeDef>& defs, Graph& graph) const;
  void RejectCycles(const Graph& graph) const;

  const NodeRegistry& registry_;
};

}  // namespace lark
