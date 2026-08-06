// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

namespace lark::rpc {

// ─────────────────────────────────────────────────────────────────────────────
// RpcCode / RpcStatus: transport-agnostic call result.
// ─────────────────────────────────────────────────────────────────────────────
enum class RpcCode {
  kOk = 0,
  kDeadline,        // call exceeded its configured timeout
  kNotFound,        // unknown service / method
  kUnavailable,     // server or endpoint unreachable
  kInvalidArgument, // malformed request / target
  kInternal,        // handler error or backend failure
};

inline const char* ToString(RpcCode code) noexcept {
  switch (code) {
    case RpcCode::kOk:
      return "ok";
    case RpcCode::kDeadline:
      return "deadline";
    case RpcCode::kNotFound:
      return "not_found";
    case RpcCode::kUnavailable:
      return "unavailable";
    case RpcCode::kInvalidArgument:
      return "invalid_argument";
    case RpcCode::kInternal:
      return "internal";
  }
  return "unknown";
}

class RpcStatus {
 public:
  RpcStatus() = default;
  RpcStatus(RpcCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  static RpcStatus Ok() { return RpcStatus{RpcCode::kOk, {}}; }
  static RpcStatus Deadline(std::string msg) {
    return RpcStatus{RpcCode::kDeadline, std::move(msg)};
  }
  static RpcStatus NotFound(std::string msg) {
    return RpcStatus{RpcCode::kNotFound, std::move(msg)};
  }
  static RpcStatus Unavailable(std::string msg) {
    return RpcStatus{RpcCode::kUnavailable, std::move(msg)};
  }
  static RpcStatus InvalidArgument(std::string msg) {
    return RpcStatus{RpcCode::kInvalidArgument, std::move(msg)};
  }
  static RpcStatus Internal(std::string msg) {
    return RpcStatus{RpcCode::kInternal, std::move(msg)};
  }

  bool ok() const noexcept { return code_ == RpcCode::kOk; }
  RpcCode code() const noexcept { return code_; }
  const std::string& message() const noexcept { return message_; }
  explicit operator bool() const noexcept { return ok(); }

  std::string ToString() const {
    std::string out = ::lark::rpc::ToString(code_);
    if (!message_.empty()) {
      out += ": ";
      out += message_;
    }
    return out;
  }

 private:
  RpcCode code_ = RpcCode::kOk;
  std::string message_;
};

}  // namespace lark::rpc
