#pragma once

#include <string>
#include <string_view>

#include "adapter.hh"

namespace tsugumi::abi9 {

auto RunNodeCard(tsugumi_bytes input) -> Tsugumi::Card::CardResultT;

// Verifies a FlatBuffer produced or received by the Node.js runtime.
template <class Table>
auto VerifyNodeValue(
    tsugumi_bytes bytes, std::string_view name, FailureKind kind,
    void (*verify_merge)(const Table&, std::string_view, FailureKind) = nullptr)
    -> void {
  const Buffer buffer(bytes);
  const auto* value = buffer.Verify<Table>();
  if (!value) Reject(kind, "Invalid FlatBuffer for " + std::string(name));
  if (verify_merge) verify_merge(*value, name, kind);
}

inline auto VerifyFlatBufferOutput(const Tsugumi::Card::CardPortT& port)
    -> void {
  if (port.kind != PortKind::FlatBuffer || !port.path.empty())
    Reject(FailureKind::output_invalid, "Invalid output " + port.name);
}

inline auto VerifyPathOutput(const Tsugumi::Card::CardPortT& port) -> void {
  if (port.kind != PortKind::Path || !port.bytes.empty() ||
      port.path.empty() || port.path.find('\0') != std::string::npos)
    Reject(FailureKind::output_invalid, "Invalid Path output " + port.name);
}

}  // namespace tsugumi::abi9
