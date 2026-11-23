#pragma once

#include <string>
#include <string_view>

#include "adapter.hh"

namespace tsugumi::abi {

auto RunNodeCard(tsugumi_bytes input) -> Tsugumi::Card::CardResultT;

}  // namespace tsugumi::abi
