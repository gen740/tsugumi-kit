#pragma once

#include "adapter.hh"

namespace tsugumi::abi9 {

auto RunPython(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT;

template <class Table>
auto VerifyOutput(const Tsugumi::Card::CardPortT& port) -> void {
  const Buffer buffer({port.bytes.data(), port.bytes.size()});
  if (!buffer.Verify<Table>())
    Reject(FailureKind::output_invalid, "Invalid output " + port.name);
}

}  // namespace tsugumi::abi9
