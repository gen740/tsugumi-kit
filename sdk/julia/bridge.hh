#pragma once

#include <nlohmann/json.hpp>

#include "adapter.hh"

namespace tsugumi::julia {

using Json = nlohmann::json;

auto RunJulia(const std::string& input) -> Json;
auto ReadFailure(const Json& failure) -> Tsugumi::Card::CardResultT;

}  // namespace tsugumi::julia
