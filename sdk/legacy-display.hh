#pragma once

#include <flatbuffers/base.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "tsugumi/abi9/card_runtime.hh"

namespace tsugumi::legacy_display {

// R and Julia still emit the ABI 9 legacy display names and payloads.
inline auto Emit(std::string_view kind, const nlohmann::json& config,
                 bool record, std::span<const card::Blob> blobs = {}) -> int {
  return card::display(
      card::Module{"tsugumi:" + std::string(kind), config.dump(), blobs},
      record);
}

inline auto EmitImage(std::string_view media_type, std::string_view alt,
                      std::span<const uint8_t> bytes, bool record) -> int {
  const card::Blob blobs[] = {{"image", bytes}};
  return Emit("image", {{"media_type", media_type}, {"alt", alt}}, record,
              blobs);
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
                       std::span<const uint32_t> colors, bool record) -> int {
  const size_t count = x.size();
  if (y.size() != count || z.size() != count ||
      (!colors.empty() && colors.size() != count)) {
    return 1;
  }
  const auto x_bytes = LittleEndianBytes(x);
  const auto y_bytes = LittleEndianBytes(y);
  const auto z_bytes = LittleEndianBytes(z);
  const auto color_bytes = LittleEndianBytes(colors);
  const card::Blob blobs[] = {
      {"x", x_bytes}, {"y", y_bytes}, {"z", z_bytes}, {"colors", color_bytes}};
  return Emit("points", {{"count", count}}, record, blobs);
}

struct LineSeries {
  std::string_view id;
  std::string_view label;
  std::span<const double> x;
  std::span<const double> y;
};

inline auto EmitLineChart(std::span<const LineSeries> rows, bool record)
    -> int {
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
  const card::Blob blobs[] = {{"x", x}, {"y", y}};
  return Emit("line-chart", {{"series", series}}, record, blobs);
}

}  // namespace tsugumi::legacy_display
