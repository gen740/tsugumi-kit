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

auto AddPath(CardInputT& input, const char* name, const std::string& path)
    -> void {
  auto port = std::make_unique<CardPortT>();
  port->name = name;
  port->kind = PortKind::Path;
  port->path = path;
  input.inputs.push_back(std::move(port));
}

auto ReadPath(const CardResult& output, size_t index, std::string_view name)
    -> std::string {
  const auto* port = output.outputs()->Get(index);
  CHECK(port->name()->string_view() == name && port->kind() == PortKind::Path);
  CHECK(!port->bytes() || port->bytes()->size() == 0);
  CHECK(port->path());
  return port->path()->str();
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

auto MakeInput(input_case scenario, int mode, int fd, bool zero_root)
    -> tsugumi_bytes {
  static constexpr std::array<uint8_t, 3> short_buffer{1, 2, 3};
  static constexpr std::array<uint8_t, 12> zero_offset{};
  const auto bad =
      zero_root ? tsugumi_bytes{zero_offset.data(), zero_offset.size()}
                : tsugumi_bytes{short_buffer.data(), short_buffer.size()};
  if (scenario == MALFORMED_ENVELOPE) {
    return bad;
  }

  CardInputT input;
  parameters::ParamsT params;
  params.mode = mode;
  params.fd = fd;
  input.params = Encode<parameters::Params>(params);
  if (scenario == MALFORMED_PARAMS)
    input.params.assign(bad.data, bad.data + bad.size);

  if (scenario != MISSING_INPUT) {
    rust_inputs::number::NumberT number;
    number.position = std::make_unique<rust_inputs::number::Position>(40, 2);
    const auto kind = scenario == UNKNOWN_KIND ? static_cast<PortKind>(255)
                      : scenario == WRONG_KIND ? PortKind::Path
                                               : PortKind::FlatBuffer;
    AddPort(input, "number", kind,
            scenario == MALFORMED_PORT
                ? std::vector<uint8_t>(bad.data, bad.data + bad.size)
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
    const auto file = std::filesystem::absolute("input.txt").string();
    std::ofstream(file) << "card data\n";
    AddPath(input, "file", file);
    const auto directory = std::filesystem::absolute("input-dir").string();
    std::filesystem::create_directory(directory);
    AddPath(input, "directory", directory);
  }

  if (mode == 21 || mode == 22 || mode == 23 || mode == 26) {
    using namespace rust_inputs::arrival::tsugumi::merge;
    MergedT merged;
    if (mode == 26) {
      t1::ReadingT value;
      value.name = "reading";
      ReadingBoxT box;
      box.bytes = Encode<t1::Reading>(value);
      merged.value.Set(std::move(box));
    } else if (mode != 23) {
      t0::nested::t0::TemperatureT value;
      value.value = 41.5;
      t0::nested::TemperatureBoxT temperature;
      temperature.bytes = mode == 22
                              ? std::vector<uint8_t>{1, 2, 3}
                              : Encode<t0::nested::t0::Temperature>(value);
      t0::nested::MergedT nested;
      nested.value.Set(std::move(temperature));
      MergedBoxT box;
      box.bytes = Encode<t0::nested::Merged>(nested);
      merged.value.Set(std::move(box));
    }
    AddPort(input, "arrival", PortKind::FlatBuffer, Encode<Merged>(merged));
  }
  if (mode == 25) {
    AddPath(input, "file", "relative.txt");
  }

  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(CardInput::Pack(builder, &input));
  input_buffer = builder.Release();
  return {input_buffer.data(), input_buffer.size()};
}

}  // namespace

extern "C" auto make_input(input_case scenario, int mode, int fd)
    -> tsugumi_bytes {
  return MakeInput(scenario, mode, fd, false);
}

extern "C" auto check_zero_roots(tsugumi_main_fn entry) -> void {
  const std::array cases{
      std::pair{MALFORMED_ENVELOPE, "Invalid CardInput"},
      std::pair{MALFORMED_PARAMS, "Invalid FlatBuffer for params"},
      std::pair{MALFORMED_PORT, "Invalid FlatBuffer for number"},
  };
  for (const auto& [scenario, message] : cases) {
    const tsugumi_call call{TSUGUMI_ABI_VERSION,
                            MakeInput(scenario, 0, -1, true), nullptr, nullptr};
    tsugumi_bytes result{};
    CHECK(entry(&call, &result) == 0);
    CHECK(ReadFailure(result, FailureKind::invalid_request)
              .message()
              ->string_view() == message);
  }
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
  CHECK(result.outputs()->size() == (optional_ports ? 7u : 2u));
  const auto summary =
      ReadPort<rust_outputs::summary::Summary>(result, 1, "summary");
  CHECK(summary.values == (std::vector<int64_t>{3, 5}));
  CHECK(summary.data.AsLabel() && summary.data.AsLabel()->text == "choice");

  if (optional_ports) {
    const auto copied = ReadPath(result, 2, "copy");
    CHECK(copied == "copy.txt");
    CHECK(std::filesystem::is_regular_file(
        std::filesystem::symlink_status(copied)));
    std::ifstream file(copied);
    std::string line;
    CHECK(std::getline(file, line) && line == "card data");
    CHECK(ReadPath(result, 3, "forwarded") ==
          std::filesystem::absolute("input.txt"));
    CHECK(ReadPath(result, 4, "forwarded_directory") ==
          std::filesystem::absolute("input-dir"));
    const auto point = ReadPort<rust_outputs::_2d::Value>(result, 5, "_2d");
    CHECK(point.x == 40 && point.y == 2);
    const auto async = ReadPort<rust_outputs::async::Value>(result, 6, "async");
    CHECK(async.data.AsLabel() && async.data.AsLabel()->text == "choice");
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
    case 12:
      CHECK(ReadFailure(bytes, FailureKind::worker_failed)
                .message()
                ->string_view() == "Card panicked");
      break;
    case 6:
    case 7:
      CHECK(ReadFailure(bytes, FailureKind::output_invalid)
                .message()
                ->string_view() ==
            (mode == 6 ? "Invalid output async" : "Invalid output summary"));
      break;
    case 8:
      CHECK(
          ReadFailure(bytes, FailureKind::cancelled).message()->string_view() ==
          "host cancelled");
      break;
    case 10:
    case 11:
      CHECK(ReadFailure(bytes, FailureKind::worker_failed)
                .message()
                ->string_view() ==
            (mode == 10 ? "unknown failure kind" : "reserved failure kind"));
      break;
    case 13:
      CHECK(ReadFailure(bytes, FailureKind::worker_failed)
                .message()
                ->string_view() == "Display failed (status 73)");
      break;
    case 14:
    case 15:
    case 16: {
      static constexpr const char* messages[] = {
          "View names cannot contain a NUL byte",
          "Point coordinates and colors must have matching lengths",
          "Line coordinates must be finite and have matching lengths"};
      CHECK(ReadFailure(bytes, FailureKind::invalid_request)
                .message()
                ->string_view() == messages[mode - 14]);
      break;
    }
    default:
      CHECK(false);
  }
}

extern "C" auto check_input_failure(tsugumi_bytes result, input_case scenario)
    -> void {
  const auto& failure = ReadFailure(result, scenario == MISSING_INPUT
                                                ? FailureKind::input_missing
                                                : FailureKind::invalid_request);
  if (scenario == MISSING_INPUT)
    CHECK(failure.message()->string_view() == "Missing input number");
}

extern "C" auto check_revision2(tsugumi_main_fn entry) -> void {
  using namespace rust_outputs::arrival::tsugumi::merge;
  for (const int mode : {20, 21, 22, 23, 25, 26}) {
    const tsugumi_call call{TSUGUMI_ABI_VERSION, make_input(VALID, mode, -1),
                            nullptr, nullptr};
    tsugumi_bytes result{};
    CHECK(entry(&call, &result) == 0);
    if (mode == 20) {
      CHECK(ReadFailure(result, FailureKind::input_missing)
                .message()
                ->string_view() == "Missing input unwired");
    } else if (mode == 22 || mode == 23 || mode == 25) {
      ReadFailure(result, FailureKind::invalid_request);
    } else if (mode == 24) {
      ReadFailure(result, FailureKind::output_invalid);
    } else {
      const auto& decoded = Decode<CardResult>(result);
      CHECK(!decoded.failure() && decoded.outputs()->size() == 3);
      const auto merged = ReadPort<Merged>(decoded, 2, "arrival");
      if (mode == 21) {
        const auto* box = merged.value.AsMergedBox();
        CHECK(box);
        t0::nested::MergedT nested;
        Decode<t0::nested::Merged>({box->bytes.data(), box->bytes.size()})
            .UnPackTo(&nested);
        const auto* member = nested.value.AsTemperatureBox();
        CHECK(member);
        CHECK(Decode<t0::nested::t0::Temperature>(
                  {member->bytes.data(), member->bytes.size()})
                  .value() == 42.5);
      } else {
        const auto* box = merged.value.AsReadingBox();
        CHECK(box);
        CHECK(Decode<t1::Reading>({box->bytes.data(), box->bytes.size()})
                  .name()
                  ->string_view() == "reading");
      }
    }
  }
}
