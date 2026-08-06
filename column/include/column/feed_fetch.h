// Copyright (c) 2024 LARK Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <variant>
#include <vector>

#include "column/tensor_table.h"

namespace lark::column {

// ─────────────────────────────────────────────────────────────────────────────
// Row-oriented business data.
//
// Business code typically deals with rows (dict-like records); the engine
// computes over columns. Cell is a tagged union of the supported numeric
// types. Columns whose cells contain any double are fed as float64, otherwise
// as int64.
// ─────────────────────────────────────────────────────────────────────────────
using Cell = std::variant<std::monostate, int64_t, double>;
using Row = std::vector<Cell>;

// feed (行转列): convert row-oriented records into a columnar TensorTable.
//   * `names` are the column names; every row must have at least `names.size()`
//     cells.
//   * A column that contains any double value is stored as float64, otherwise
//     int64. Missing cells (monostate) are zero-filled.
// Throws std::runtime_error on malformed rows.
TensorTable feed_table(const std::vector<std::string>& names,
                       const std::vector<Row>& rows);

// Convenience: build a TensorTable from explicit named columns.
TensorTable table_from_columns(
    std::vector<std::pair<std::string, Tensor>> columns);

// fetch (列转行): convert requested columns of a columnar table back into
// rows. Int columns become int64 cells; float/quantized columns become double
// cells. All requested columns must share the same row count.
std::vector<Row> fetch_rows(const TensorTable& table,
                            const std::vector<std::string>& names);
// Fetch every column (in insertion order).
std::vector<Row> fetch_rows(const TensorTable& table);

// Fetch a single value from a length-1 tensor.
double fetch_scalar(const Tensor& t);

}  // namespace lark::column
