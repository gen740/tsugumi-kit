#include "adapter.hh"

extern "C" int tsugumi_go_run(uint8_t* data, size_t size, void* result);

namespace {

auto RunCard(tsugumi_bytes input) -> Tsugumi::Card::CardResultT {
  const tsugumi::abi::Buffer buffer(input);
  if (!buffer.Verify<Tsugumi::Card::CardInput>()) {
    tsugumi::abi::Reject(tsugumi::abi::FailureKind::invalid_request,
                          "Invalid CardInput");
  }
  Tsugumi::Card::CardResultT result;
  // Go immediately makes an owned copy and never writes to these bytes.
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
