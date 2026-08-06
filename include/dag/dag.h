#pragma once

// Umbrella header for the DAG framework. Include this to get the full public
// API: Context, Node, NodeRegistry (+ DAG_REGISTER_NODE), Graph, GraphBuilder,
// Monitor and Executor, plus the coroutine primitives.

#include "dag/context.h"
#include "dag/coro/async_event.h"
#include "dag/coro/fire_and_forget.h"
#include "dag/coro/task.h"
#include "dag/coro/thread_pool.h"
#include "dag/default_context.h"
#include "dag/executor.h"
#include "dag/graph.h"
#include "dag/graph_builder.h"
#include "dag/i_context.h"
#include "dag/monitor.h"
#include "dag/node.h"
#include "dag/node_registry.h"
#include "dag/schedule.h"
#include "dag/stats.h"
