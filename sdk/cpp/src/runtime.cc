#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter.hh"
#include "tsugumi/abi9/card_runtime.hh"

namespace tsugumi {

Failure::Failure(Tsugumi::Data::FailureKind kind, const std::string& message,
                 std::source_location location, std::string cell)
    : std::runtime_error(message),
      kind(kind),
      location(location),
      cell(std::move(cell)) {}

}  // namespace tsugumi

namespace card {
namespace {

using nlohmann::json;

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

}  // namespace

auto display(Module module, bool record) -> int {
  using Tsugumi::Card::CardDisplayBlob;
  using Tsugumi::Card::CreateCardDisplay;
  using Tsugumi::Card::CreateCardDisplayBlob;
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<CardDisplayBlob>> blobs;
  blobs.reserve(module.blobs.size());
  for (const auto& blob : module.blobs) {
    const auto name = builder.CreateString(blob.name);
    const auto bytes =
        builder.CreateVector(blob.bytes.data(), blob.bytes.size());
    blobs.push_back(CreateCardDisplayBlob(builder, name, bytes));
  }
  const auto id = builder.CreateString(module.module);
  const auto config = builder.CreateString(module.config);
  builder.Finish(
      CreateCardDisplay(builder, id, config, builder.CreateVector(blobs)));
  return tsugumi::abi9::SendDisplay(
      {builder.GetBufferPointer(), builder.GetSize()}, record);
}

auto display(std::string_view text, bool record) -> int {
  return display(Module{"tsugumi:text", json{{"text", text}}.dump()}, record);
}

auto display(Markdown markdown, bool record) -> int {
  return display(
      Module{"tsugumi:markdown", json{{"text", markdown.text}}.dump()}, record);
}

auto display(double value, bool record) -> int {
  return display(Scalar{value}, record);
}

auto display(Scalar scalar, bool record) -> int {
  const auto config = json{{"value", scalar.value}, {"label", scalar.label}};
  return display(Module{"tsugumi:scalar", config.dump()}, record);
}

auto display(Table table, bool record) -> int {
  const auto config = json{{"columns", table.columns}, {"rows", table.rows}};
  return display(Module{"tsugumi:table", config.dump()}, record);
}

auto display(Image image, bool record) -> int {
  const auto config =
      json{{"media_type", image.media_type}, {"alt", image.alt}};
  const Blob blobs[] = {{"image", image.bytes}};
  return display(Module{"tsugumi:image", config.dump(), blobs}, record);
}

auto display(Points points, bool record) -> int {
  const size_t count = points.x.size();
  if (points.y.size() != count || points.z.size() != count ||
      (!points.colors.empty() && points.colors.size() != count)) {
    return 1;
  }
  const auto x = LittleEndianBytes(points.x);
  const auto y = LittleEndianBytes(points.y);
  const auto z = LittleEndianBytes(points.z);
  const auto colors = LittleEndianBytes(points.colors);
  const Blob blobs[] = {{"x", x}, {"y", y}, {"z", z}, {"colors", colors}};
  return display(Module{"tsugumi:points", json{{"count", count}}.dump(), blobs},
                 record);
}

auto display(LineChart chart, bool record) -> int {
  const auto finite = [](double value) { return std::isfinite(value); };
  auto series = json::array();
  std::vector<uint8_t> x;
  std::vector<uint8_t> y;
  size_t offset = 0;
  for (const auto& row : chart.series) {
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
  const Blob blobs[] = {{"x", x}, {"y", y}};
  return display(
      Module{"tsugumi:line-chart", json{{"series", series}}.dump(), blobs},
      record);
}

auto display(Plotly plot, bool record) -> int {
  return display(Module{"tsugumi:plotly", plot.config}, record);
}

auto display(VegaLite plot, bool record) -> int {
  return display(Module{"tsugumi:vega-lite", plot.config}, record);
}

}  // namespace card
