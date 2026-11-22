#pragma once

#include <cstdint>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "tsugumi/protocol/tsugumi_enums_generated.h"

namespace tsugumi {

struct Failure : std::runtime_error {
  Failure(Tsugumi::Data::FailureKind kind, const std::string& message,
          std::source_location location = std::source_location::current(),
          std::string cell = {});

  Tsugumi::Data::FailureKind kind;
  std::source_location location;
  std::string cell;
};

}  // namespace tsugumi

namespace card {
namespace display {

struct Markdown {
  std::string_view text;
};

struct Scalar {
  double value;
  std::string_view label = {};
};

struct Table {
  std::span<const std::string> columns;
  std::span<const std::vector<std::string>> rows;
};

struct Image {
  std::string_view media_type;
  std::span<const uint8_t> bytes;
  std::string_view alt = {};
};

struct Points {
  std::span<const float> x;
  std::span<const float> y;
  std::span<const float> z;
  std::span<const uint32_t> colors = {};
};

struct Series {
  std::string_view id;
  std::string_view label;
  std::span<const double> x;
  std::span<const double> y;
};

struct LineChart {
  std::span<const Series> series;
};

// The bytes of text, borrowed from the string.
inline auto text_bytes(std::string_view text) -> std::span<const uint8_t> {
  return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

auto send(std::string_view view, std::span<const uint8_t> bytes) -> int;
auto send(std::string_view text) -> int;
auto send(Markdown markdown) -> int;
auto send(double value) -> int;
auto send(Scalar scalar) -> int;
auto send(Table table) -> int;
auto send(Image image) -> int;
auto send(Points points) -> int;
auto send(LineChart chart) -> int;

}  // namespace display

auto cancelled() -> bool;
auto remaining_ms() -> int64_t;

}  // namespace card
