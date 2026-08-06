// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/biz/module.h"

#include <utility>

#include "column/biz/dsl.h"

namespace lark::column::biz {

const std::string Module::kEmptyDsl;

Module::Module(std::string name) : name_(std::move(name)) {}

Module& Module::input(std::string port) {
  inputs_.push_back(std::move(port));
  return *this;
}

Module& Module::output(std::string port) {
  outputs_.push_back(std::move(port));
  return *this;
}

Module& Module::depends_on(std::string module_name) {
  deps_.push_back(std::move(module_name));
  return *this;
}

Module& Module::op(exec::OpSpec spec) {
  subgraph_.op(std::move(spec));
  return *this;
}

Module& Module::from_dsl(const std::string& source) {
  dsl_ = source;
  for (exec::OpSpec& spec : dsl::parse(source)) {
    subgraph_.op(std::move(spec));
  }
  return *this;
}

}  // namespace lark::column::biz
