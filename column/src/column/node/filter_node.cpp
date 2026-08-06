// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/node/filter_node.h"

#include <cstring>
#include <stdexcept>

namespace lark::column {

FilterNode::FilterNode(MaskFn mask_fn) : mask_fn_(std::move(mask_fn)) {}

TensorTable FilterNode::execute(TensorTable input) const {
  Tensor mask = mask_fn_(input);
  if (mask.dtype() != DType::kInt64)
    throw std::runtime_error("FilterNode: mask must be int64");

  const size_t n = mask.size();
  const auto* mask_data = mask.data_as<int64_t>();

  // Count passing rows
  size_t keep_count = 0;
  for (size_t i = 0; i < n; ++i) {
    if (mask_data[i]) ++keep_count;
  }

  // Build filtered table
  TensorTable filtered;
  for (const auto& col_name : input.names()) {
    const auto& src = input.get(col_name);
    const size_t elem_size = dtype_size(src.dtype());

    Tensor dst(src.dtype(), keep_count);
    const uint8_t* src_data = src.data();
    uint8_t* dst_data = dst.mutable_data();

    size_t j = 0;
    for (size_t i = 0; i < n; ++i) {
      if (mask_data[i]) {
        std::memcpy(dst_data + j * elem_size,
                    src_data + i * elem_size, elem_size);
        ++j;
      }
    }
    dst.resize(keep_count);
    filtered.add(col_name, std::move(dst));
  }
  return filtered;
}

const char* FilterNode::node_type() const noexcept { return "FilterNode"; }

}  // namespace lark::column
