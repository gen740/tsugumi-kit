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
#include "tsugumi/abi9/card_runtime.hh"

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
  return tsugumi::abi9::SendDisplay(view,
                                    {text_bytes(text).data(), text.size()});
}

}  // namespace

auto send(std::string_view view, std::span<const uint8_t> bytes) -> int {
  if (view.find('\0') != std::string_view::npos) return 1;
  return tsugumi::abi9::SendDisplay(std::string(view).c_str(),
                                    {bytes.data(), bytes.size()});
}

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
  const auto text = nlohmann::json{
      {"media_type", image.media_type},
      {"alt",
       image.alt}}.dump();
  if (text.size() > std::numeric_limits<uint32_t>::max()) return 1;
  std::vector<uint8_t> bytes(sizeof(uint32_t));
  flatbuffers::WriteScalar(bytes.data(), static_cast<uint32_t>(text.size()));
  const auto metadata = text_bytes(text);
  bytes.insert(bytes.end(), metadata.begin(), metadata.end());
  bytes.insert(bytes.end(), image.bytes.begin(), image.bytes.end());
  return send("image", bytes);
}

}  // namespace card::display
