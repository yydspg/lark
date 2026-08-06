// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

// Umbrella header for the generic RPC framework.
//
// OOP design: business code programs against interfaces (RpcService,
// RpcChannel, RpcServer, RpcBackend); concrete frameworks (in-process / gRPC /
// brpc) plug in behind the RpcFactory. Backends translate their wire format to
///from the transport-agnostic RpcMessage.

#include "rpc/backend.h"
#include "rpc/call_options.h"
#include "rpc/channel.h"
#include "rpc/endpoint.h"
#include "rpc/inproc_backend.h"
#include "rpc/message.h"
#include "rpc/server.h"
#include "rpc/service.h"
#include "rpc/status.h"

// gRPC / brpc adapters (functional only when LARK_WITH_GRPC / LARK_WITH_BRPC).
#include "rpc/brpc_backend.h"
#include "rpc/grpc_backend.h"
