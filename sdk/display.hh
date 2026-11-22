#pragma once

#include <nlohmann/json.hpp>
#include <string_view>

#include "tsugumi/abi/card_runtime.hh"

namespace tsugumi::display {

inline auto Emit(std::string_view view, const nlohmann::json& config) -> int {
  return card::display::send(view, card::display::text_bytes(config.dump()));
}

}  // namespace tsugumi::display
