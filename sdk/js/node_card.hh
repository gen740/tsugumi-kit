#pragma once

#include <span>

#include "adapter.hh"

namespace tsugumi::abi {

auto RunNodeCard(std::span<const tsugumi_input> inputs) -> ResultBuffer;

}  // namespace tsugumi::abi
