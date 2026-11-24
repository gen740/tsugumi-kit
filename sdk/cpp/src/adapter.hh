#pragma once

#include <flatbuffers/flatbuffers.h>

#include <cstdint>
#include <cstdlib>
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

using CardBody = auto (*)(tsugumi_bytes) -> Tsugumi::Card::CardResultT;
auto Invoke(const tsugumi_call* call, tsugumi_bytes* result,
            CardBody body) noexcept -> int;

using WireBody = auto (*)(tsugumi_bytes, flatbuffers::FlatBufferBuilder&)
    -> void;
auto Invoke(const tsugumi_call* call, tsugumi_bytes* result,
            WireBody body) noexcept -> int;

class ResultBuffer {
 public:
  ResultBuffer() = default;
  explicit ResultBuffer(size_t size);
  explicit ResultBuffer(tsugumi_bytes bytes);

  auto data() const -> uint8_t* { return data_.get(); }
  auto bytes() const -> tsugumi_bytes { return {data(), size_}; }
  auto release() -> tsugumi_bytes { return {data_.release(), size_}; }

 private:
  std::unique_ptr<uint8_t, decltype(&std::free)> data_{nullptr, std::free};
  size_t size_ = 0;
};

using SerializedBody = auto (*)(tsugumi_bytes) -> ResultBuffer;
auto Invoke(const tsugumi_call* call, tsugumi_bytes* result,
            SerializedBody body) noexcept -> int;

auto SendDisplay(const tsugumi_view_session& session, tsugumi_bytes bytes)
    -> int;

// A byte span, especially a nested ubyte vector, can start at an address
// that cannot hold the alignment of the types in the FlatBuffer.
class Buffer {
 public:
  explicit Buffer(tsugumi_bytes bytes, bool align = true);
  auto needs_alignment() const -> bool {
    return reinterpret_cast<uintptr_t>(data_) % alignof(Block) != 0;
  }
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
    Reject(FailureKind::input_invalid,
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

  struct Port {
    std::string_view schema;
    tsugumi_bytes bytes;
  };

  auto ReadPort(std::string_view name) -> std::optional<Port>;
  auto CheckConsumed() const -> void;

  template <class Table>
  auto ReadTable(tsugumi_bytes bytes, std::string_view name) -> const Table* {
    const Buffer direct(bytes, false);
    const auto& buffer =
        direct.needs_alignment()
            ? *aligned_.emplace_back(std::make_unique<Buffer>(bytes))
            : direct;
    if (const auto* value = buffer.Verify<Table>()) return value;
    Reject(FailureKind::input_invalid,
           "Invalid FlatBuffer for " + std::string(name));
  }

 private:
  Buffer buffer_;
  std::vector<std::unique_ptr<Buffer>> aligned_;
  const Tsugumi::Card::CardInput* input_;
  std::unordered_map<std::string_view, const Tsugumi::Card::CardPort*> ports_;
};

auto CheckSchema(const Input::Port& port, std::string_view schema,
                 std::string_view name) -> void;

template <class Table>
auto WritePort(Tsugumi::Card::CardResultT& result, std::string_view name,
               const std::optional<typename Table::NativeTableType>& value,
               bool required,
               bool (*valid)(const typename Table::NativeTableType&)) -> void {
  if (!value) {
    if (required) {
      Reject(FailureKind::output_invalid,
             "Missing output " + std::string(name));
    }
    return;
  }
  auto port = std::make_unique<Tsugumi::Card::CardPortT>();
  port->name = name;
  if (!valid(*value)) {
    Reject(FailureKind::output_invalid, "Invalid output " + std::string(name));
  }
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Table::Pack(builder, &*value));
  flatbuffers::Verifier verifier(builder.GetBufferPointer(), builder.GetSize());
  if (!verifier.VerifyBuffer<Table>()) {
    Reject(FailureKind::output_invalid, "Invalid output " + std::string(name));
  }
  port->bytes.assign(builder.GetBufferPointer(),
                     builder.GetBufferPointer() + builder.GetSize());
  result.outputs.push_back(std::move(port));
}

template <class Table>
auto WriteWirePort(flatbuffers::FlatBufferBuilder& result,
                   std::string_view name,
                   const std::optional<typename Table::NativeTableType>& value,
                   bool required,
                   bool (*valid)(const typename Table::NativeTableType&))
    -> flatbuffers::Offset<Tsugumi::Card::CardPort> {
  if (!value) {
    if (required)
      Reject(FailureKind::output_invalid,
             "Missing output " + std::string(name));
    return {};
  }
  if (!valid(*value))
    Reject(FailureKind::output_invalid, "Invalid output " + std::string(name));

  flatbuffers::FlatBufferBuilder payload;
  payload.Finish(Table::Pack(payload, &*value));
  flatbuffers::Verifier verifier(payload.GetBufferPointer(), payload.GetSize());
  if (!verifier.VerifyBuffer<Table>())
    Reject(FailureKind::output_invalid, "Invalid output " + std::string(name));

  return Tsugumi::Card::CreateCardPort(
      result, result.CreateString(name), {},
      result.CreateVector(payload.GetBufferPointer(), payload.GetSize()));
}

// The open failure message for an Artifact ID of 16 bytes.
auto ArtifactUnavailable(const uint8_t* id) -> std::string;

}  // namespace tsugumi::abi
