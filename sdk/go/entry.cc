#include "adapter.hh"

extern "C" int tsugumi_go_run(uint8_t* data, size_t size, void* result);

namespace {

auto RunCard(tsugumi_bytes input) -> Tsugumi::Card::CardResultT {
  const tsugumi::abi::Buffer buffer(input);
  if (!buffer.Verify<Tsugumi::Card::CardInput>()) {
    tsugumi::abi::Reject(tsugumi::abi::FailureKind::input_invalid,
                         "Invalid CardInput");
  }
  Tsugumi::Card::CardResultT result;
  // Go borrows the envelope while decoding and owns any bytes exposed to Card
  // code.
  if (tsugumi_go_run(const_cast<uint8_t*>(input.data), input.size, &result)) {
    throw std::bad_alloc();
  }
  return result;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) auto tsugumi_go_entry(
    const tsugumi_call* call, tsugumi_bytes* result) -> int {
  return tsugumi::abi::Invoke(call, result, RunCard);
}
