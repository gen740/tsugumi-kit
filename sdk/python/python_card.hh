#pragma once

#include "adapter.hh"

namespace tsugumi::abi {

// Borrows the host ports while Python copies the selected values.
class PythonInput {
 public:
  using Port = std::pair<std::string_view, Input::Port>;

  explicit PythonInput(const tsugumi_call& call) : input_(call) {}

  auto Take(std::string_view name) -> std::optional<Input::Port> {
    const auto port = input_.ReadPort(name);
    if (!port)
      Reject(FailureKind::input_missing, "Missing input " + std::string(name));
    return port;
  }

  template <class Table>
  auto Read(std::string_view name, Input::Port port, std::string_view schema)
      -> void {
    const Buffer buffer(port.bytes);
    if (!buffer.Verify<Table>())
      Reject(FailureKind::input_invalid,
             "Invalid FlatBuffer for " + std::string(name));
    port.schema = schema;
    ports_.emplace_back(name, port);
  }

  auto Ports() const -> const std::vector<Port>& { return ports_; }
  auto CheckConsumed() const -> void { input_.CheckConsumed(); }

 private:
  Input input_;
  std::vector<Port> ports_;
};

auto RunPython(const PythonInput& input) -> Tsugumi::Card::CardResultT;

template <class Table>
auto VerifyOutput(Tsugumi::Card::CardPortT& port,
                  bool (*valid)(const typename Table::NativeTableType&))
    -> void {
  const Buffer buffer({port.bytes.data(), port.bytes.size()});
  const auto* wire = buffer.Verify<Table>();
  if (!wire) Reject(FailureKind::output_invalid, "Invalid output " + port.name);
  typename Table::NativeTableType value;
  wire->UnPackTo(&value);
  if (!valid(value))
    Reject(FailureKind::output_invalid, "Invalid output " + port.name);
}

}  // namespace tsugumi::abi
