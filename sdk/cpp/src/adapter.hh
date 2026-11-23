#pragma once

#include <flatbuffers/flatbuffers.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tsugumi/abi/card_abi.h"
#include "tsugumi/abi/card_runtime.hh"
#include "tsugumi/protocol/card_generated.h"

namespace tsugumi::abi {

using Tsugumi::Data::FailureKind;
using Tsugumi::Data::PortKind;

using CardBody = auto (*)(tsugumi_bytes) -> Tsugumi::Card::CardResultT;
auto Invoke(const tsugumi_call* call, tsugumi_bytes* result,
            CardBody body) noexcept -> int;

auto SendDisplay(const char* view, tsugumi_bytes bytes) -> int;

// A byte span, especially a nested ubyte vector, can start at an address
// that cannot hold the alignment of the types in the FlatBuffer.
class Buffer {
 public:
  explicit Buffer(tsugumi_bytes bytes);
  Buffer(const Buffer&) = delete;
  auto operator=(const Buffer&) -> Buffer& = delete;

  template <class Table>
  auto Verify() const -> const Table* {
    if (!data_) return nullptr;
    flatbuffers::Verifier verifier(data_, size_);
    return verifier.VerifyBuffer<Table>() ? flatbuffers::GetRoot<Table>(data_)
                                          : nullptr;
  }

 private:
  struct alignas(FLATBUFFERS_MAX_ALIGNMENT) Block {
    uint8_t bytes[FLATBUFFERS_MAX_ALIGNMENT];
  };

  std::vector<Block> aligned_;
  const uint8_t* data_ = nullptr;
  size_t size_;
};

[[noreturn]] auto Reject(FailureKind kind, const std::string& message) -> void;

template <class Table, bool AllowEmpty = false>
auto ReadValue(tsugumi_bytes bytes, std::string_view name) ->
    typename Table::NativeTableType {
  if constexpr (AllowEmpty) {
    if (bytes.size == 0) return {};
  }
  Buffer buffer(bytes);
  const auto* value = buffer.Verify<Table>();
  if (!value) {
    Reject(FailureKind::invalid_request,
           "Invalid FlatBuffer for " + std::string(name));
  }
  typename Table::NativeTableType result;
  value->UnPackTo(&result);
  return result;
}

// Owns any alignment copy of the envelope while its port views are used.
class Input {
 public:
  explicit Input(tsugumi_bytes bytes);

  template <class Table>
  auto ReadPort(std::string_view name)
      -> std::optional<typename Table::NativeTableType> {
    const auto bytes = ReadPortBytes(name);
    return bytes ? std::optional{ReadValue<Table>(*bytes, name)} : std::nullopt;
  }

  auto ReadPortBytes(std::string_view name) -> std::optional<tsugumi_bytes>;
  auto ReadPath(std::string_view name) -> std::optional<std::filesystem::path>;
  auto Params() const -> tsugumi_bytes;
  auto CheckConsumed() const -> void;

 private:
  auto TakePort(std::string_view name, PortKind kind)
      -> const Tsugumi::Card::CardPort*;

  Buffer buffer_;
  const Tsugumi::Card::CardInput* input_;
  std::unordered_map<std::string_view, const Tsugumi::Card::CardPort*> ports_;
};

auto WritePath(Tsugumi::Card::CardResultT& result, std::string_view name,
               const std::optional<std::filesystem::path>& path, bool required)
    -> void;

template <class Table>
auto PackValue(
    const typename Table::NativeTableType& value, std::string_view name,
    bool (*is_packable)(const typename Table::NativeTableType&) = nullptr)
    -> std::vector<uint8_t> {
  if (is_packable && !is_packable(value)) {
    Reject(FailureKind::output_invalid, "Invalid output " + std::string(name));
  }
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Table::Pack(builder, &value));
  flatbuffers::Verifier verifier(builder.GetBufferPointer(), builder.GetSize());
  if (!verifier.VerifyBuffer<Table>()) {
    Reject(FailureKind::output_invalid, "Invalid output " + std::string(name));
  }
  return {builder.GetBufferPointer(),
          builder.GetBufferPointer() + builder.GetSize()};
}

template <class Table>
auto WritePort(
    Tsugumi::Card::CardResultT& result, std::string_view name,
    const std::optional<typename Table::NativeTableType>& value, bool required,
    bool (*is_packable)(const typename Table::NativeTableType&) = nullptr)
    -> void {
  if (!value) {
    if (required) {
      Reject(FailureKind::output_invalid,
             "Missing output " + std::string(name));
    }
    return;
  }
  auto port = std::make_unique<Tsugumi::Card::CardPortT>();
  port->name = name;
  port->kind = PortKind::FlatBuffer;
  port->bytes = PackValue<Table>(*value, name, is_packable);
  result.outputs.push_back(std::move(port));
}

}  // namespace tsugumi::abi
