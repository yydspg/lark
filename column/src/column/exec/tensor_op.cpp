// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/exec/tensor_op.h"

#include <stdexcept>

namespace lark::column::exec {

const Tensor& OpContext::input(const std::string& name) const {
  const Tensor* t = store_->find(name);
  if (t == nullptr) {
    throw std::out_of_range("OpContext::input: unknown tensor '" + name + "'");
  }
  return *t;
}

}  // namespace lark::column::exec
