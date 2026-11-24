#pragma once

#include <nlohmann/json.hpp>
#include <string_view>

#include "adapter.hh"
#include "tsugumi/abi/card_runtime.hh"

namespace tsugumi::display {

inline auto Emit(std::string_view view, const nlohmann::json& config) -> int {
  const auto opened = card::display::view_open(view);
  const auto text = config.dump();
  const auto bytes = card::display::text_bytes(text);
  return abi::SendDisplay(opened.session, {bytes.data(), bytes.size()});
}

}  // namespace tsugumi::display
