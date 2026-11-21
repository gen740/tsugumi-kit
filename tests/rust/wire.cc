#include "wire.h"

#include <array>
#include <filesystem>
#include <fstream>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "_params_generated.h"
#include "tsugumi/protocol/card_generated.h"

namespace {

using Tsugumi::Card::CardFailure;
using Tsugumi::Card::CardInput;
using Tsugumi::Card::CardInputT;
using Tsugumi::Card::CardPort;
using Tsugumi::Card::CardPortT;
using Tsugumi::Card::CardResult;
using Tsugumi::Data::FailureKind;
using Tsugumi::Data::PortKind;

flatbuffers::DetachedBuffer input_buffer;

template <typename Table>
auto Decode(tsugumi_bytes bytes) -> const Table& {
  CHECK(bytes.data && bytes.size);
  flatbuffers::Verifier verifier(bytes.data, bytes.size);
  CHECK(verifier.VerifyBuffer<Table>());
  return *flatbuffers::GetRoot<Table>(bytes.data);
}

template <typename Table>
auto Encode(const typename Table::NativeTableType& value)
    -> std::vector<uint8_t> {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Table::Pack(builder, &value));
  return {builder.GetBufferPointer(),
          builder.GetBufferPointer() + builder.GetSize()};
}

auto AddPort(CardInputT& input, const char* name, PortKind kind,
             std::vector<uint8_t> bytes) -> void {
  auto port = std::make_unique<CardPortT>();
  port->name = name;
  port->kind = kind;
  port->bytes = std::move(bytes);
  input.inputs.push_back(std::move(port));
}

template <typename Table>
auto ReadPort(const CardResult& result, size_t index, std::string_view name,
              PortKind kind = PortKind::FlatBuffer) ->
    typename Table::NativeTableType {
  CHECK(result.outputs() && index < result.outputs()->size());
  const auto* port = result.outputs()->Get(index);
  CHECK(port->name()->string_view() == name && port->kind() == kind);
  const auto* bytes = port->bytes();
  CHECK(bytes && bytes->size());
  // Nested ubyte vectors are allowed to be unaligned for C++ scalar accesses.
  std::vector<uint64_t> aligned((bytes->size() + 7) / 8);
  std::memcpy(aligned.data(), bytes->data(), bytes->size());
  typename Table::NativeTableType value;
  Decode<Table>(
      {reinterpret_cast<const uint8_t*>(aligned.data()), bytes->size()})
      .UnPackTo(&value);
  return value;
}

auto ReadFailure(tsugumi_bytes bytes, FailureKind kind) -> const CardFailure& {
  const auto& result = Decode<CardResult>(bytes);
  CHECK(!result.outputs() || !result.outputs()->size());
  CHECK(result.failure() && result.failure()->kind() == kind);
  CHECK(result.failure()->message() && result.failure()->message()->size());
  return *result.failure();
}

}  // namespace

extern "C" auto make_input(input_case scenario, int mode, int fd)
    -> tsugumi_bytes {
  constexpr std::array<uint8_t, 3> bad{1, 2, 3};
  if (scenario == MALFORMED_ENVELOPE) {
    static constexpr auto malformed = bad;
    return {malformed.data(), malformed.size()};
  }

  CardInputT input;
  parameters::ParamsT params;
  params.mode = mode;
  params.fd = fd;
  input.params = Encode<parameters::Params>(params);
  if (scenario == MALFORMED_PARAMS) input.params.assign(bad.begin(), bad.end());

  if (scenario != MISSING_INPUT) {
    rust_inputs::number::NumberT number;
    number.value = 42;
    const auto kind = scenario == UNKNOWN_KIND ? static_cast<PortKind>(255)
                      : scenario == WRONG_KIND ? PortKind::File
                                               : PortKind::FlatBuffer;
    AddPort(input, "number", kind,
            scenario == MALFORMED_PORT
                ? std::vector<uint8_t>(bad.begin(), bad.end())
                : Encode<rust_inputs::number::Number>(number));
    if (scenario == UNKNOWN_PORT)
      AddPort(input, "unknown", kind,
              Encode<rust_inputs::number::Number>(number));
    if (scenario == DUPLICATE_PORT)
      AddPort(input, "number", kind,
              Encode<rust_inputs::number::Number>(number));
  }

  rust_inputs::payload::PayloadT payload;
  rust_inputs::payload::LabelT label;
  label.text = "choice";
  payload.data.Set(std::move(label));
  payload.values = {3, 5};
  AddPort(input, "payload", PortKind::FlatBuffer,
          Encode<rust_inputs::payload::Payload>(payload));

  if (scenario == OPTIONAL_PORTS) {
    rust_inputs::file::FileT file;
    file.value = std::filesystem::absolute("input.txt").string();
    std::ofstream(file.value) << "card data\n";
    AddPort(input, "file", PortKind::File,
            Encode<rust_inputs::file::File>(file));
  }

  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(CardInput::Pack(builder, &input));
  input_buffer = builder.Release();
  return {input_buffer.data(), input_buffer.size()};
}

extern "C" auto check_number(tsugumi_bytes bytes, int64_t expected) -> void {
  const auto& result = Decode<CardResult>(bytes);
  CHECK(!result.failure());
  CHECK(ReadPort<rust_outputs::number::Number>(result, 0, "number").value ==
        expected);
}

extern "C" auto check_success(tsugumi_bytes bytes, int optional_ports) -> void {
  const auto& result = Decode<CardResult>(bytes);
  check_number(bytes, 42);
  CHECK(result.outputs()->size() == (optional_ports ? 4u : 2u));
  const auto summary =
      ReadPort<rust_outputs::summary::Summary>(result, 1, "summary");
  CHECK(summary.values == (std::vector<int64_t>{3, 5}));
  CHECK(summary.data.AsLabel() && summary.data.AsLabel()->text == "choice");

  if (optional_ports) {
    const auto copied =
        ReadPort<rust_outputs::file::File>(result, 2, "copy", PortKind::File)
            .value;
    CHECK(copied == "copy.txt");
    CHECK(std::filesystem::is_regular_file(
        std::filesystem::symlink_status(copied)));
    std::ifstream file(copied);
    std::string line;
    CHECK(std::getline(file, line) && line == "card data");
    const auto forwarded = ReadPort<rust_outputs::file::File>(
                               result, 3, "forwarded", PortKind::File)
                               .value;
    CHECK(forwarded == std::filesystem::absolute("input.txt"));
  }
}

extern "C" auto check_card_failure(tsugumi_bytes bytes, int mode) -> void {
  switch (mode) {
    case 3: {
      const auto& failure = ReadFailure(bytes, FailureKind::cancelled);
      CHECK(failure.message()->string_view() == "explicit failure");
      CHECK(failure.source() &&
            failure.source()->string_view() == "card/src/lib.rs");
      CHECK(failure.line() > 0);
      CHECK(failure.cell() && failure.cell()->string_view() == "fit");
      break;
    }
    case 4:
      CHECK(ReadFailure(bytes, FailureKind::worker_failed)
                .message()
                ->string_view() == "standard exception");
      break;
    case 5:
      CHECK(ReadFailure(bytes, FailureKind::worker_failed)
                .message()
                ->string_view() == "Card panicked");
      break;
    case 8:
      CHECK(
          ReadFailure(bytes, FailureKind::cancelled).message()->string_view() ==
          "host cancelled");
      break;
    default:
      ReadFailure(bytes, FailureKind::output_invalid);
      break;
  }
}
