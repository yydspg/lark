// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#include "column/feed_fetch.h"

#include <stdexcept>

namespace lark::column {

TensorTable feed_table(const std::vector<std::string>& names,
                       const std::vector<Row>& rows) {
  TensorTable table;

  for (size_t c = 0; c < names.size(); ++c) {
    bool any_double = false;
    for (const auto& row : rows) {
      if (c >= row.size()) {
        throw std::runtime_error("feed_table: row has fewer cells than columns");
      }
      const Cell& cell = row[c];
      if (std::holds_alternative<double>(cell)) any_double = true;
    }

    const DType dtype = any_double ? DType::kFloat64 : DType::kInt64;
    Tensor col(dtype, rows.size());
    col.resize(rows.size());
    for (size_t r = 0; r < rows.size(); ++r) {
      const Cell& cell = rows[r][c];
      if (std::holds_alternative<std::monostate>(cell)) {
        if (dtype == DType::kInt64)
          col.set<int64_t>(r, 0);
        else
          col.set<double>(r, 0.0);
      } else if (std::holds_alternative<int64_t>(cell)) {
        const int64_t v = std::get<int64_t>(cell);
        if (dtype == DType::kInt64)
          col.set<int64_t>(r, v);
        else
          col.set<double>(r, static_cast<double>(v));
      } else {
        const double v = std::get<double>(cell);
        if (dtype == DType::kInt64)
          col.set<int64_t>(r, static_cast<int64_t>(v));
        else
          col.set<double>(r, v);
      }
    }
    table.add(names[c], std::move(col));
  }
  return table;
}

TensorTable table_from_columns(
    std::vector<std::pair<std::string, Tensor>> columns) {
  TensorTable table;
  for (auto& [name, tensor] : columns) {
    table.add(name, std::move(tensor));
  }
  return table;
}

std::vector<Row> fetch_rows(const TensorTable& table,
                            const std::vector<std::string>& names) {
  if (names.empty()) return {};
  const size_t rows = table.get(names[0]).size();
  for (const auto& name : names) {
    if (!table.has(name))
      throw std::runtime_error("fetch_rows: unknown column '" + name + "'");
    if (table.get(name).size() != rows)
      throw std::runtime_error("fetch_rows: column '" + name +
                               "' length mismatch");
  }

  std::vector<Row> result(rows, Row(names.size()));
  for (size_t c = 0; c < names.size(); ++c) {
    const Tensor& col = table.get(names[c]);
    for (size_t r = 0; r < rows; ++r) {
      if (is_int(col.dtype())) {
        switch (col.dtype()) {
          case DType::kInt32:
            result[r][c] = Cell(static_cast<int64_t>(col.get<int32_t>(r)));
            break;
          default:
            result[r][c] = Cell(col.get<int64_t>(r));
            break;
        }
      } else {
        switch (col.dtype()) {
          case DType::kFloat32:
            result[r][c] = Cell(static_cast<double>(col.get<float>(r)));
            break;
          default:
            result[r][c] = Cell(col.get<double>(r));
            break;
        }
      }
    }
  }
  return result;
}

std::vector<Row> fetch_rows(const TensorTable& table) {
  return fetch_rows(table, table.names());
}

double fetch_scalar(const Tensor& t) {
  if (t.size() == 0) return 0.0;
  switch (t.dtype()) {
    case DType::kInt32:
      return static_cast<double>(t.get<int32_t>(0));
    case DType::kInt64:
      return static_cast<double>(t.get<int64_t>(0));
    case DType::kFloat32:
      return static_cast<double>(t.get<float>(0));
    case DType::kFloat64:
      return t.get<double>(0);
    case DType::kQ8_0:
      return fetch_scalar(t.dequantize());
  }
  return 0.0;
}

}  // namespace lark::column
