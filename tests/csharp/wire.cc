#include "wire.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "_params_generated.h"
#include "tsugumi/protocol/card_generated.h"

namespace {

using namespace Tsugumi::Card;
using Tsugumi::Data::FailureKind;
using Tsugumi::Data::PortKind;

flatbuffers::DetachedBuffer input_buffer;
int final_observation = 0;

template <class Table>
auto Decode(tsugumi_bytes bytes) -> const Table& {
  CHECK(bytes.data && bytes.size);
  flatbuffers::Verifier verifier(bytes.data, bytes.size);
  CHECK(verifier.VerifyBuffer<Table>());
  return *flatbuffers::GetRoot<Table>(bytes.data);
}

template <class Table>
auto DecodePort(const CardPort& port, std::string_view name, PortKind kind) ->
    typename Table::NativeTableType {
  CHECK(port.name()->string_view() == name && port.kind() == kind);
  const auto& data = *port.bytes();
  std::vector<uint64_t> storage((data.size() + 7) / 8);
  std::memcpy(storage.data(), data.data(), data.size());
  typename Table::NativeTableType value;
  Decode<Table>({reinterpret_cast<const uint8_t*>(storage.data()), data.size()})
      .UnPackTo(&value);
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

}  // namespace

extern "C" auto make_input(input_case scenario, int mode, int fd)
    -> tsugumi_bytes {
  static constexpr uint8_t bad_bytes[] = {1, 2, 3};
  if (scenario == MALFORMED_ENVELOPE) return {bad_bytes, sizeof(bad_bytes)};

  // The common host checks the borrowed result before it runs mode 2.
  if (mode == 2) final_observation = 1;

  CardInputT input;
  parameters::ParamsT params;
  params.mode = mode;
  params.fd = fd;
  input.params = Encode<parameters::Params>(params);
  if (scenario == MALFORMED_PARAMS) input.params = {1, 2, 3};

  inputs::sample::SampleT sample;
  sample.name = "table payload";
  inputs::sample::CountT count;
  count.value = 2;
  sample.value.Set(count);
  auto sample_bytes = Encode<inputs::sample::Sample>(sample);
  if (scenario == MALFORMED_PORT) {
    // The verifier must reject a bad union discriminator in an otherwise
    // structurally valid port.
    auto* root = flatbuffers::GetMutableRoot<inputs::sample::Sample>(
        sample_bytes.data());
    auto* table = reinterpret_cast<flatbuffers::Table*>(root);
    CHECK(table->SetField<uint8_t>(inputs::sample::Sample::VT_VALUE_TYPE, 255));
  }
  AddPort(input, "sample", PortKind::FlatBuffer, std::move(sample_bytes));

  if (scenario != MISSING_INPUT) {
    inputs::number::NumberT number;
    number.value = 40;
    const auto kind = scenario == UNKNOWN_KIND ? static_cast<PortKind>(255)
                      : scenario == WRONG_KIND ? PortKind::File
                                               : PortKind::FlatBuffer;
    AddPort(input, "number", kind, Encode<inputs::number::Number>(number));
    if (scenario == DUPLICATE_PORT)
      AddPort(input, "number", kind, Encode<inputs::number::Number>(number));
    if (scenario == UNKNOWN_PORT)
      AddPort(input, "unknown", kind, Encode<inputs::number::Number>(number));
  }
  if (scenario == OPTIONAL_PORTS) {
    inputs::file::FileT file;
    file.value = std::filesystem::absolute("input.txt").string();
    inputs::directory::DirectoryT directory;
    directory.value = std::filesystem::absolute("input-dir").string();
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
  if (const auto* failure = decoded.failure())
    std::fprintf(stderr, "Unexpected CardFailure %u: %s\n",
                 static_cast<unsigned>(failure->kind()),
                 failure->message() ? failure->message()->c_str() : "");
  CHECK(!decoded.failure() && decoded.outputs());
  CHECK(decoded.outputs()->size() == (optional_ports ? 4u : 3u));
  const auto& ports = *decoded.outputs();
  const auto expected_number = final_observation == 2 ? 43 : 42;
  if (final_observation) ++final_observation;
  CHECK(DecodePort<outputs::number::Number>(*ports.Get(0), "number",
                                            PortKind::FlatBuffer)
            .value == expected_number);
  const auto sample = DecodePort<outputs::out::Sample>(*ports.Get(1), "out",
                                                       PortKind::FlatBuffer);
  CHECK(sample.name == "table payload");
  CHECK(sample.value.AsLabel() &&
        sample.value.AsLabel()->value == "union result");

  const auto file =
      DecodePort<outputs::file::File>(*ports.Get(2), "file", PortKind::File)
          .value;
  if (!optional_ports) {
    CHECK(file == "created.txt" && std::filesystem::is_regular_file(file));
    std::string contents;
    std::ifstream stream(file);
    CHECK(std::getline(stream, contents) && contents == "table payload");
  } else {
    CHECK(file == std::filesystem::absolute("input.txt").string());
    CHECK(DecodePort<outputs::directory::Directory>(*ports.Get(3), "directory",
                                                    PortKind::Directory)
              .value == std::filesystem::absolute("input-dir").string());
  }
}

extern "C" auto check_input_failure(tsugumi_bytes result, input_case scenario)
    -> void {
  Failure(result, scenario == MISSING_INPUT ? FailureKind::input_missing
                                            : FailureKind::invalid_request);
}

extern "C" auto check_card_failure(tsugumi_bytes result, int mode) -> void {
  switch (mode) {
    case 3: {
      const auto& failure = Failure(result, FailureKind::cancelled);
      CHECK(failure.message()->str() == "explicit failure");
      CHECK(failure.source() && failure.source()->str() == "src/Card.cs");
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
  if (index == 4 || index == 9) {
    const auto* blob = display.blobs()->Get(0);
    CHECK(blob->name()->string_view() == (index == 4 ? "image" : "sample"));
    CHECK(
        std::ranges::equal(*blob->bytes(), std::array<uint8_t, 3>{0, 13, 255}));
  }
}
