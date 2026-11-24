#pragma once

#include <string>
#include <string_view>

#include "adapter.hh"

namespace tsugumi::abi {

auto RunNodeCard(tsugumi_bytes input) -> ResultBuffer;

}  // namespace tsugumi::abi
