#include <cmath>
#include <cstdint>
#include <cstdio>
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

namespace card {
namespace {

using nlohmann::json;

// The bundled views read their settings from a "config" blob of UTF-8 JSON.
auto display_config(std::string_view view, const json& config, bool record,
                    std::span<const Blob> data = {}) -> int {
  const auto text = config.dump();
  std::vector<Blob> blobs{{"config", text_bytes(text)}};
  blobs.insert(blobs.end(), data.begin(), data.end());
  return display(View{view, blobs}, record);
}

}  // namespace

auto display(View view, bool record) -> int {
  using Tsugumi::Card::CardDisplayBlob;
  using Tsugumi::Card::CreateCardDisplay;
  using Tsugumi::Card::CreateCardDisplayBlob;
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<CardDisplayBlob>> blobs;
  blobs.reserve(view.blobs.size());
  for (const auto& blob : view.blobs) {
    const auto name = builder.CreateString(blob.name);
    const auto bytes =
        builder.CreateVector(blob.bytes.data(), blob.bytes.size());
    blobs.push_back(CreateCardDisplayBlob(builder, name, bytes));
  }
  const auto name = builder.CreateString(view.view);
  builder.Finish(CreateCardDisplay(builder, name, builder.CreateVector(blobs)));
  return tsugumi::abi9::SendDisplay(
      {builder.GetBufferPointer(), builder.GetSize()}, record);
}

auto display(std::string_view text, bool record) -> int {
  return display_config("text", {{"text", text}}, record);
}

auto display(Markdown markdown, bool record) -> int {
  return display_config("markdown", {{"text", markdown.text}}, record);
}

auto display(double value, bool record) -> int {
  return display(Scalar{value}, record);
}

auto display(Scalar scalar, bool record) -> int {
  char number[32] = "\xe2\x80\x94";
  if (std::isfinite(scalar.value))
    std::snprintf(number, sizeof number, "%.6g", scalar.value);
  auto text = std::string{};
  if (!scalar.label.empty()) text = "**" + std::string{scalar.label} + "**: ";
  text += number;
  return display(Markdown{text}, record);
}

auto display(Table table, bool record) -> int {
  return display_config(
      "table", {{"columns", table.columns}, {"rows", table.rows}}, record);
}

auto display(Image image, bool record) -> int {
  const Blob blobs[] = {{"image", image.bytes}};
  return display_config("image",
                        {{"media_type", image.media_type}, {"alt", image.alt}},
                        record, blobs);
}

}  // namespace card
