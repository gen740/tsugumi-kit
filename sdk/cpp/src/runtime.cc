#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter.hh"
#include "tsugumi/abi/card_runtime.hh"

namespace tsugumi {

Failure::Failure(Tsugumi::Data::FailureKind kind, const std::string& message,
                 std::source_location location, std::string cell)
    : std::runtime_error(message),
      kind(kind),
      location(location),
      cell(std::move(cell)) {}

}  // namespace tsugumi

namespace card::display {
namespace {

auto SendJson(const char* view, const nlohmann::json& config) -> int {
  const auto text = config.dump();
  return tsugumi::abi::SendDisplay(view_open(view).session,
                                   {text_bytes(text).data(), text.size()});
}

auto Frame(const nlohmann::json& config) -> std::vector<uint8_t> {
  const auto text = config.dump();
  if (text.size() > std::numeric_limits<uint32_t>::max())
    throw std::length_error("Display metadata exceeds the frame size");
  std::vector<uint8_t> bytes(sizeof(uint32_t));
  flatbuffers::WriteScalar(bytes.data(), static_cast<uint32_t>(text.size()));
  const auto metadata = text_bytes(text);
  bytes.insert(bytes.end(), metadata.begin(), metadata.end());
  return bytes;
}

template <class T>
auto AppendSamples(std::vector<uint8_t>& bytes, std::span<const T> samples)
    -> void {
  for (auto value : samples) {
    value = flatbuffers::EndianScalar(value);
    const auto* data = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), data, data + sizeof(value));
  }
}

}  // namespace

auto send(std::string_view text) -> int {
  return SendJson("text", {{"text", text}});
}

auto send(Markdown markdown) -> int {
  return SendJson("markdown", {{"text", markdown.text}});
}

auto send(double value) -> int { return send(Scalar{value}); }

auto send(Scalar scalar) -> int {
  char number[32] = "\xe2\x80\x94";
  if (std::isfinite(scalar.value))
    std::snprintf(number, sizeof number, "%.6g", scalar.value);
  auto text = std::string{};
  if (!scalar.label.empty()) text = "**" + std::string{scalar.label} + "**: ";
  text += number;
  return send(Markdown{text});
}

auto send(Table table) -> int {
  return SendJson("table", {{"columns", table.columns}, {"rows", table.rows}});
}

auto send(Image image) -> int {
  auto bytes = Frame({{"media_type", image.media_type}, {"alt", image.alt}});
  bytes.insert(bytes.end(), image.bytes.begin(), image.bytes.end());
  return tsugumi::abi::SendDisplay(view_open("image").session,
                                   {bytes.data(), bytes.size()});
}

auto send(Points points) -> int {
  return send(view_open("points").session, points);
}

auto send(const tsugumi_view_session& session, Points points) -> int {
  const auto count = points.x.size();
  if (points.y.size() != count || points.z.size() != count ||
      (!points.colors.empty() && points.colors.size() != count)) {
    throw std::invalid_argument(
        "Points require matching x, y, z and optional RGB colors");
  }
  auto bytes = Frame({{"count", count}});
  AppendSamples(bytes, points.x);
  AppendSamples(bytes, points.y);
  AppendSamples(bytes, points.z);
  AppendSamples(bytes, points.colors);
  return tsugumi::abi::SendDisplay(session, {bytes.data(), bytes.size()});
}

auto send(LineChart chart) -> int {
  return send(view_open("line-chart").session, chart);
}

auto send(const tsugumi_view_session& session, LineChart chart) -> int {
  const auto finite = [](double value) { return std::isfinite(value); };
  auto series = nlohmann::json::array();
  size_t offset = 0;
  for (const auto& row : chart.series) {
    if (row.x.size() != row.y.size() || !std::ranges::all_of(row.x, finite) ||
        !std::ranges::all_of(row.y, finite)) {
      throw std::invalid_argument(
          "Line chart axes require matching finite values");
    }
    series.push_back({{"id", row.id},
                      {"label", row.label},
                      {"offset", offset},
                      {"count", row.x.size()}});
    offset += row.x.size();
  }
  auto bytes = Frame({{"series", series}});
  for (const auto& row : chart.series) AppendSamples(bytes, row.x);
  for (const auto& row : chart.series) AppendSamples(bytes, row.y);
  return tsugumi::abi::SendDisplay(session, {bytes.data(), bytes.size()});
}

}  // namespace card::display
