#pragma once

#include "adapter.hh"

namespace tsugumi::abi9 {

template <class Table>
auto VerifyValue(tsugumi_bytes bytes, std::string_view name) -> void {
  const Buffer buffer(bytes);
  if (!buffer.Verify<Table>())
    Reject(FailureKind::invalid_request,
           "Invalid FlatBuffer for " + std::string(name));
}

// Keep the verified envelope alive while Python copies its nested buffers.
class PythonInput {
 public:
  explicit PythonInput(tsugumi_bytes bytes);

  template <class Table>
  auto VerifyPort(std::string_view name, PortKind kind, bool required) -> void {
    if (const auto* port = TakePort(name, kind, required)) {
      const auto* bytes = port->bytes();
      VerifyValue<Table>(
          bytes ? tsugumi_bytes{bytes->data(), bytes->size()} : tsugumi_bytes{},
          name);
    }
  }

  auto Envelope() const -> const Tsugumi::Card::CardInput& { return *input_; }
  auto Params() const -> tsugumi_bytes;
  auto CheckConsumed() const -> void;

 private:
  auto TakePort(std::string_view name, PortKind kind, bool required)
      -> const Tsugumi::Card::CardPort*;

  Buffer buffer_;
  const Tsugumi::Card::CardInput* input_;
  std::unordered_map<std::string_view, const Tsugumi::Card::CardPort*> ports_;
};

auto RunPython(const Tsugumi::Card::CardInput& input)
    -> Tsugumi::Card::CardResultT;

template <class Table>
auto VerifyOutput(const Tsugumi::Card::CardPortT& port) -> void {
  const Buffer buffer({port.bytes.data(), port.bytes.size()});
  if (!buffer.Verify<Table>())
    Reject(FailureKind::output_invalid, "Invalid output " + port.name);
}

}  // namespace tsugumi::abi9
