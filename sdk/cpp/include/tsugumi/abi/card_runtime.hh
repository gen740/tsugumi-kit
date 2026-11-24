#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "tsugumi/abi/card_abi.h"
#include "tsugumi/protocol/artifact_generated.h"
#include "tsugumi/protocol/card_enums_generated.h"

namespace tsugumi {

struct Failure : std::runtime_error {
  Failure(Tsugumi::Data::FailureKind kind, const std::string& message,
          std::source_location location = std::source_location::current(),
          std::string cell = {});

  Tsugumi::Data::FailureKind kind;
  std::source_location location;
  std::string cell;
};

namespace abi::detail {

template <std::size_t Size>
struct InputName {
  char text[Size];

  // A string literal can be used as a template argument in the generated API.
  constexpr InputName(const char (&name)[Size]) {
    std::copy_n(name, Size, text);
  }
};

template <class T>
inline constexpr bool kIsOptional = false;

template <class T>
inline constexpr bool kIsOptional<std::optional<T>> = true;

template <class T>
auto MissingInput(std::string_view name) -> T {
  if constexpr (kIsOptional<T>) {
    return std::nullopt;
  } else {
    throw Failure(Tsugumi::Data::FailureKind::input_missing,
                  "Missing input " + std::string(name), {});
  }
}

template <class T, class U>
auto GetInput(std::string_view name, const std::optional<U>& value) -> T {
  if (!value) return MissingInput<T>(name);
  if constexpr (std::same_as<T, std::optional<U>>) {
    return value;
  } else if constexpr (std::same_as<T, U>) {
    return *value;
  } else {
    throw Failure(Tsugumi::Data::FailureKind::invalid_request,
                  "Wrong C++ type for input " + std::string(name), {});
  }
}

}  // namespace abi::detail

}  // namespace tsugumi

namespace card {
using Artifact = tsugumi::ArtifactT;

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

struct OpenedView {
  tsugumi_route route;
  tsugumi_view_session session;
};

enum class ReceiveStatus { kMessage, kClose, kTimeout, kCancelled };

struct Received {
  ReceiveStatus status;
  tsugumi_route route{};
  tsugumi_view_session session{};
  std::vector<uint8_t> bytes;
};

auto view_open(std::string_view view, uint32_t mode = TSUGUMI_VIEW_PANEL,
               uint32_t width = 0, uint32_t height = 0) -> OpenedView;
auto route_register(const tsugumi_view_session& session) -> tsugumi_route;
auto route_unregister(const tsugumi_route& route) -> void;
auto recv(int64_t timeout_ms = -1) -> Received;
auto send(const tsugumi_view_session& session, std::span<const uint8_t> bytes)
    -> int;
// Send one view ABI message without the SDK's HTML framing.
auto send_raw(const tsugumi_view_session& session,
              std::span<const uint8_t> bytes) -> int;
auto send(std::string_view text) -> int;
auto send(Markdown markdown) -> int;
auto send(double value) -> int;
auto send(Scalar scalar) -> int;
auto send(Table table) -> int;
auto send(Image image) -> int;
auto send(Points points) -> int;
auto send(const tsugumi_view_session& session, Points points) -> int;
auto send(LineChart chart) -> int;
auto send(const tsugumi_view_session& session, LineChart chart) -> int;

}  // namespace display

auto cancelled() -> bool;
auto remaining_ms() -> int64_t;

}  // namespace card
