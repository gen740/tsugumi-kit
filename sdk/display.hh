#pragma once

#include <flatbuffers/base.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "tsugumi/abi9/card_runtime.hh"

namespace tsugumi::display {

// Binary views read a little-endian metadata length, UTF-8 JSON, and
// then the view-specific binary sections in order.
inline auto Emit(std::string_view view, const nlohmann::json& config,
                 std::initializer_list<std::span<const uint8_t>> parts = {})
    -> int {
  const auto text = config.dump();
  const auto metadata = card::display::text_bytes(text);
  if (parts.size() == 0) return card::display::send(view, metadata);
  if (metadata.size() > std::numeric_limits<uint32_t>::max()) return 1;
  std::vector<uint8_t> bytes(sizeof(uint32_t));
  flatbuffers::WriteScalar(bytes.data(),
                           static_cast<uint32_t>(metadata.size()));
  bytes.insert(bytes.end(), metadata.begin(), metadata.end());
  for (const auto part : parts) {
    bytes.insert(bytes.end(), part.begin(), part.end());
  }
  return card::display::send(view, bytes);
}

template <class T>
auto AppendBytes(std::vector<uint8_t>& bytes, std::span<const T> values)
    -> void {
  for (auto value : values) {
    value = flatbuffers::EndianScalar(value);
    const auto* data = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), data, data + sizeof(value));
  }
}

template <class T>
auto LittleEndianBytes(std::span<const T> values) -> std::vector<uint8_t> {
  std::vector<uint8_t> bytes;
  bytes.reserve(values.size_bytes());
  AppendBytes(bytes, values);
  return bytes;
}

inline auto EmitPoints(std::span<const float> x, std::span<const float> y,
                       std::span<const float> z,
                       std::span<const uint32_t> colors) -> int {
  const size_t count = x.size();
  if (y.size() != count || z.size() != count ||
      (!colors.empty() && colors.size() != count)) {
    return 1;
  }
  const auto x_bytes = LittleEndianBytes(x);
  const auto y_bytes = LittleEndianBytes(y);
  const auto z_bytes = LittleEndianBytes(z);
  const auto color_bytes = LittleEndianBytes(colors);
  return Emit("points", {{"count", count}},
              {x_bytes, y_bytes, z_bytes, color_bytes});
}

struct LineSeries {
  std::string_view id;
  std::string_view label;
  std::span<const double> x;
  std::span<const double> y;
};

inline auto EmitLineChart(std::span<const LineSeries> rows) -> int {
  const auto finite = [](double value) { return std::isfinite(value); };
  auto series = nlohmann::json::array();
  std::vector<uint8_t> x;
  std::vector<uint8_t> y;
  size_t offset = 0;
  for (const auto& row : rows) {
    if (row.x.size() != row.y.size() || !std::ranges::all_of(row.x, finite) ||
        !std::ranges::all_of(row.y, finite)) {
      return 1;
    }
    series.push_back({{"id", row.id},
                      {"label", row.label},
                      {"offset", offset},
                      {"count", row.x.size()}});
    AppendBytes(x, row.x);
    AppendBytes(y, row.y);
    offset += row.x.size();
  }
  return Emit("line-chart", {{"series", series}}, {x, y});
}

}  // namespace tsugumi::display
