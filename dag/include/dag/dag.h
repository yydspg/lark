#pragma once

// Umbrella header for the DAG framework. Include this to get the full public
// API: Context, Node, NodeRegistry (+ DAG_REGISTER_NODE), Graph, GraphBuilder,
// Executor and per-node timing stats, plus the coroutine primitives and the
// unified monitoring abstraction (lark::monitor::Monitor).

#include "dag/context.h"
#include "coro/async_event.h"
#include "coro/fire_and_forget.h"
#include "coro/task.h"
#include "coro/thread_pool.h"
#include "dag/default_context.h"
#include "dag/executor.h"
#include "dag/graph.h"
#include "dag/graph_builder.h"
#include "dag/i_context.h"
#include "dag/node.h"
#include "dag/node_registry.h"
#include "dag/schedule.h"
#include "dag/stats.h"
#include "monitor/monitor.h"
