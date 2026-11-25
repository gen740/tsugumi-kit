#include "adapter.hh"
#include "host.h"

extern "C" int tsugumi_go_run(void* inputs, size_t count, void* result);

auto tsugumi_go_read_input(const void* inputs, size_t index)
    -> tsugumi_go_input {
  const auto& input = static_cast<const tsugumi_input*>(inputs)[index];
  return {input.name, input.schema, input.bytes.data, input.bytes.size};
}

namespace {

auto RunCard(const tsugumi_call& call) -> Tsugumi::Card::CardResultT {
  Tsugumi::Card::CardResultT result;
  // Go borrows the input ports while decoding and owns any bytes exposed to
  // Card code.
  if (tsugumi_go_run(const_cast<tsugumi_input*>(call.inputs), call.input_count,
                     &result)) {
    throw std::bad_alloc();
  }
  return result;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) auto tsugumi_go_entry(
    const tsugumi_call* call, tsugumi_bytes* result) -> int {
  return tsugumi::abi::Invoke(call, result, RunCard);
}
