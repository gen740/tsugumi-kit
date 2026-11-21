#include "wire.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "_params_generated.h"
#include "tsugumi/protocol/card_generated.h"

namespace {

using namespace Tsugumi::Card;
using Tsugumi::Data::FailureKind;
using Tsugumi::Data::PortKind;

flatbuffers::DetachedBuffer input_buffer;

template <class Table>
auto Decode(tsugumi_bytes bytes) -> const Table& {
  CHECK(bytes.data && bytes.size);
  flatbuffers::Verifier verifier(bytes.data, bytes.size);
  CHECK(verifier.VerifyBuffer<Table>());
  return *flatbuffers::GetRoot<Table>(bytes.data);
}

template <class Table>
auto DecodePort(const CardPort& port) -> typename Table::NativeTableType {
  // The nested test outputs require alignment up to eight bytes.
  std::vector<uint64_t> storage((port.bytes()->size() + 7) / 8);
  std::memcpy(storage.data(), port.bytes()->data(), port.bytes()->size());
  const auto& table = Decode<Table>(
      {reinterpret_cast<const uint8_t*>(storage.data()), port.bytes()->size()});
  typename Table::NativeTableType value;
  table.UnPackTo(&value);
  return value;
}

template <class Table>
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

auto Failure(tsugumi_bytes result, FailureKind kind) -> const CardFailure& {
  const auto& decoded = Decode<CardResult>(result);
  CHECK(decoded.failure() && decoded.failure()->kind() == kind);
  CHECK(decoded.failure()->message() && decoded.failure()->message()->size());
  CHECK(!decoded.outputs() || !decoded.outputs()->size());
  return *decoded.failure();
}

auto CheckBlob(const CardDisplay& display, size_t i, std::string_view name,
               std::initializer_list<uint8_t> expected) -> void {
  const auto* blob = display.blobs()->Get(i);
  CHECK(blob->name()->string_view() == name);
  CHECK(std::ranges::equal(*blob->bytes(), expected));
}

}  // namespace

extern "C" auto make_input(input_case test_case, int mode, int fd)
    -> tsugumi_bytes {
  static constexpr uint8_t bad_bytes[] = {1, 2, 3};
  if (test_case == MALFORMED_ENVELOPE) return {bad_bytes, sizeof(bad_bytes)};

  CardInputT input;
  parameters::ParamsT params;
  params.mode = mode;
  params.fd = fd;
  input.params = Encode<parameters::Params>(params);
  if (test_case == MALFORMED_PARAMS) input.params = {1, 2, 3};
  if (test_case != MISSING_INPUT) {
    inputs::number::NumberT number;
    number.position = std::make_unique<inputs::number::Position>(40, 2);
    const auto kind = test_case == UNKNOWN_KIND ? static_cast<PortKind>(255)
                      : test_case == WRONG_KIND ? PortKind::File
                                                : PortKind::FlatBuffer;
    AddPort(input, "number", kind,
            test_case == MALFORMED_PORT
                ? std::vector<uint8_t>{1, 2, 3}
                : Encode<inputs::number::Number>(number));
    if (test_case == DUPLICATE_PORT)
      AddPort(input, "number", kind, Encode<inputs::number::Number>(number));
    if (test_case == UNKNOWN_PORT)
      AddPort(input, "unknown", kind, Encode<inputs::number::Number>(number));
  }
  if (test_case == OPTIONAL_PORTS) {
    inputs::file::FileT file;
    file.value = "/inputs/file";
    inputs::directory::DirectoryT directory;
    directory.value = "/inputs/directory";
    AddPort(input, "file", PortKind::File, Encode<inputs::file::File>(file));
    AddPort(input, "directory", PortKind::Directory,
            Encode<inputs::directory::Directory>(directory));
  }
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(CardInput::Pack(builder, &input));
  input_buffer = builder.Release();
  return {input_buffer.data(), input_buffer.size()};
}

extern "C" auto check_success(tsugumi_bytes result, int optional_ports)
    -> void {
  const auto& decoded = Decode<CardResult>(result);
  CHECK(!decoded.failure() && decoded.outputs());
  CHECK(decoded.outputs()->size() == (optional_ports ? 3u : 1u));
  const auto& number = *decoded.outputs()->Get(0);
  CHECK(number.name()->str() == "number" &&
        number.kind() == PortKind::FlatBuffer);
  CHECK(DecodePort<outputs::number::Number>(number).number->value == 42);
  if (optional_ports) {
    const auto& file = *decoded.outputs()->Get(1);
    const auto& directory = *decoded.outputs()->Get(2);
    CHECK(file.name()->str() == "file" && file.kind() == PortKind::File);
    CHECK(directory.name()->str() == "directory" &&
          directory.kind() == PortKind::Directory);
    CHECK(DecodePort<outputs::file::File>(file).value == "/inputs/file");
    CHECK(DecodePort<outputs::directory::Directory>(directory).value ==
          "/inputs/directory");
  }
}

extern "C" auto check_input_failure(tsugumi_bytes result, input_case test_case)
    -> void {
  Failure(result, test_case == MISSING_INPUT ? FailureKind::input_missing
                                             : FailureKind::invalid_request);
}

extern "C" auto check_card_failure(tsugumi_bytes result, int mode) -> void {
  switch (mode) {
    case 3: {
      const auto& failure = Failure(result, FailureKind::cancelled);
      CHECK(failure.message()->str() == "explicit failure");
      CHECK(failure.source() && failure.source()->str() == "src/main.cc");
      CHECK(failure.line() > 0);
      CHECK(failure.cell() && failure.cell()->str() == "fit");
      break;
    }
    case 4:
      CHECK(Failure(result, FailureKind::worker_failed).message()->str() ==
            "standard exception");
      break;
    case 5:
      Failure(result, FailureKind::worker_failed);
      break;
    default:
      Failure(result, FailureKind::output_invalid);
      break;
  }
}

extern "C" auto check_display(tsugumi_bytes bytes, int record, unsigned index)
    -> void {
  struct Sample {
    std::string_view module;
    std::string_view config;
    size_t blobs;
    int record = 0;
  };
  constexpr std::array samples{
      Sample{"tsugumi:text", R"({"text":"résumé\n\""})", 0},
      Sample{"tsugumi:markdown", R"({"text":"**text**"})", 0, 1},
      Sample{"tsugumi:scalar", R"({"label":"","value":2.5})", 0},
      Sample{"tsugumi:table", R"({"columns":["n"],"rows":[["1"]]})", 0},
      Sample{"tsugumi:image", R"({"alt":"sample","media_type":"image/png"})",
             1},
      Sample{"tsugumi:points", R"({"count":1})", 4},
      Sample{"tsugumi:line-chart",
             R"({"series":[{"count":1,"id":"id","label":"line","offset":0}]})",
             2},
      Sample{"tsugumi:plotly", R"({"data":[]})", 0},
      Sample{"tsugumi:vega-lite", R"({"mark":"point"})", 0},
      Sample{"custom", R"({"key":1})", 1, 1},
      Sample{"tsugumi:text", R"({"text":"outside"})", 0},
  };
  CHECK(index < samples.size());
  const auto& display = Decode<CardDisplay>(bytes);
  const auto& sample = samples[index];
  CHECK(display.module_()->string_view() == sample.module);
  CHECK(display.config()->string_view() == sample.config);
  CHECK(display.blobs() && display.blobs()->size() == sample.blobs);
  CHECK(record == sample.record);
  if (index == 4) CheckBlob(display, 0, "image", {0, 13, 255});
  if (index == 5) {
    CheckBlob(display, 0, "x", {0, 0, 128, 63});
    CheckBlob(display, 1, "y", {0, 0, 128, 63});
    CheckBlob(display, 2, "z", {0, 0, 128, 63});
    CheckBlob(display, 3, "colors", {86, 52, 18, 0});
  }
  if (index == 6) {
    CheckBlob(display, 0, "x", {0, 0, 0, 0, 0, 0, 0, 64});
    CheckBlob(display, 1, "y", {0, 0, 0, 0, 0, 0, 0, 64});
  }
  if (index == 9) CheckBlob(display, 0, "sample", {0, 13, 255});
}
