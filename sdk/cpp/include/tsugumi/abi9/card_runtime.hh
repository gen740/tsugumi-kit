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

struct Plotly {
  std::string_view config;
};

struct VegaLite {
  std::string_view config;
};

struct Blob {
  std::string_view name;
  std::span<const uint8_t> bytes;
};

struct Module {
  std::string_view module;
  std::string_view config;
  std::span<const Blob> blobs = {};
};

auto display(std::string_view text, bool record = false) -> int;
auto display(Markdown markdown, bool record = false) -> int;
auto display(double value, bool record = false) -> int;
auto display(Scalar scalar, bool record = false) -> int;
auto display(Table table, bool record = false) -> int;
auto display(Image image, bool record = false) -> int;
auto display(Points points, bool record = false) -> int;
auto display(LineChart chart, bool record = false) -> int;
auto display(Plotly plot, bool record = false) -> int;
auto display(VegaLite plot, bool record = false) -> int;
auto display(Module module, bool record = false) -> int;

auto cancelled() -> bool;
auto remaining_ms() -> int64_t;

}  // namespace card
