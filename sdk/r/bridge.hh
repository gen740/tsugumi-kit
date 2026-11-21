#pragma once

#include "json_value.hh"

namespace tsugumi::r {

// The R bridge returns either an output object or a Card failure.
auto RunR(const Json& input) -> Json;

}  // namespace tsugumi::r
