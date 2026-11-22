#pragma once

#include <variant>

#include "adapter.hh"

namespace tsugumi::abi9 {

template <class Table>
auto VerifyValue(tsugumi_bytes bytes, std::string_view name) -> void {
  const Buffer buffer(bytes);
  if (!buffer.Verify<Table>())
    Reject(FailureKind::invalid_request,
           "Invalid FlatBuffer for " + std::string(name));
}

// Input owns the verified envelope while Python copies the selected values.
class PythonInput {
 public:
  using Value = std::variant<tsugumi_bytes, std::filesystem::path>;
  using Port = std::pair<std::string_view, Value>;

  explicit PythonInput(tsugumi_bytes bytes) : input_(bytes) {}

  template <class Table>
  auto ReadPort(std::string_view name) -> void {
    if (const auto bytes = input_.ReadPortBytes(name)) {
      VerifyValue<Table>(*bytes, name);
      ports_.emplace_back(name, *bytes);
    }
  }

  auto ReadPath(std::string_view name) -> void {
    if (auto path = input_.ReadPath(name))
      ports_.emplace_back(name, std::move(*path));
  }

  auto Params() const -> tsugumi_bytes { return input_.Params(); }
  auto Ports() const -> const std::vector<Port>& { return ports_; }
  auto CheckConsumed() const -> void { input_.CheckConsumed(); }

 private:
  Input input_;
  std::vector<Port> ports_;
};

auto RunPython(const PythonInput& input) -> Tsugumi::Card::CardResultT;

template <class Table>
auto VerifyOutput(const Tsugumi::Card::CardPortT& port) -> void {
  const Buffer buffer({port.bytes.data(), port.bytes.size()});
  if (!buffer.Verify<Table>())
    Reject(FailureKind::output_invalid, "Invalid output " + port.name);
}

}  // namespace tsugumi::abi9
