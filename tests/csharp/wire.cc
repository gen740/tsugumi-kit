#include "wire.h"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <vector>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "_params_generated.h"
#include "tsugumi/protocol/card_generated.h"

extern "C" auto check_shared_abi(int argc, char** argv) -> int;

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

template <class Table, class Read>
auto ReadPort(const CardPort& port, std::string_view name, PortKind kind,
              Read read) {
  CHECK(port.name()->string_view() == name && port.kind() == kind);
  const auto& data = *port.bytes();
  std::vector<uint64_t> storage((data.size() + 7) / 8);
  std::memcpy(storage.data(), data.data(), data.size());
  return read(Decode<Table>(
      {reinterpret_cast<const uint8_t*>(storage.data()), data.size()}));
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
             std::vector<uint8_t> bytes, std::string path = {}) -> void {
  auto port = std::make_unique<CardPortT>();
  port->name = name;
  port->kind = kind;
  port->bytes = std::move(bytes);
  port->path = std::move(path);
  input.inputs.push_back(std::move(port));
}

auto Finish(const CardInputT& input) -> tsugumi_bytes {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(CardInput::Pack(builder, &input));
  input_buffer = builder.Release();
  return {input_buffer.data(), input_buffer.size()};
}

auto Failure(tsugumi_bytes result, FailureKind kind) -> const CardFailure& {
  const auto& decoded = Decode<CardResult>(result);
  CHECK(decoded.failure() && decoded.failure()->kind() == kind);
  CHECK(decoded.failure()->message() && decoded.failure()->message()->size());
  CHECK(!decoded.outputs() || !decoded.outputs()->size());
  return *decoded.failure();
}

auto CheckLateVtable(const std::vector<uint8_t>& bytes, const void* table)
    -> void {
  const auto* data = static_cast<const uint8_t*>(table);
  const auto* vtable = data - flatbuffers::ReadScalar<int32_t>(data);
  CHECK(vtable - bytes.data() > INT16_MAX);
}

auto LargeSample(bool malformed) -> std::vector<uint8_t> {
  using namespace inputs::sample;
  flatbuffers::FlatBufferBuilder builder;
  // Build the union value first so its vtable lands after the large string.
  flatbuffers::Offset<void> value;
  if (malformed) {
    const auto detail = builder.CreateString("optional detail");
    value = CreateCount(builder, 2, detail).Union();
  } else {
    value = CreateLabelDirect(builder, "late input").Union();
  }
  const auto name = builder.CreateString(std::string(40 * 1024, 'i'));
  builder.Finish(CreateSample(builder, name,
                              malformed ? Value::Count : Value::Label, value));
  std::vector<uint8_t> bytes(builder.GetBufferPointer(),
                             builder.GetBufferPointer() + builder.GetSize());
  const auto* sample = flatbuffers::GetRoot<Sample>(bytes.data());
  CheckLateVtable(bytes, sample->value());
  if (malformed) {
    const auto* detail = sample->value_as_Count()->detail();
    bytes.at(detail->Data() - bytes.data() + detail->size()) = 'x';
  }
  flatbuffers::Verifier verifier(bytes.data(), bytes.size());
  CHECK(verifier.VerifyBuffer<Sample>() == !malformed);
  return bytes;
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
  auto sample_bytes = mode == 8 || mode == 9
                          ? LargeSample(mode == 9)
                          : Encode<inputs::sample::Sample>(sample);
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
    number.position = std::make_unique<inputs::number::Position>(39, 1);
    const auto kind = scenario == UNKNOWN_KIND ? static_cast<PortKind>(255)
                      : scenario == WRONG_KIND ? PortKind::Path
                                               : PortKind::FlatBuffer;
    if (scenario == WRONG_KIND)
      AddPort(input, "number", kind, {}, "/inputs/number");
    else
      AddPort(input, "number", kind, Encode<inputs::number::Number>(number));
    if (scenario == DUPLICATE_PORT)
      AddPort(input, "number", kind, Encode<inputs::number::Number>(number));
    if (scenario == UNKNOWN_PORT)
      AddPort(input, "unknown", kind, Encode<inputs::number::Number>(number));
  }
  if (scenario == OPTIONAL_PORTS) {
    AddPort(input, "file", PortKind::Path, {},
            std::filesystem::absolute("input.txt").string());
    AddPort(input, "directory", PortKind::Path, {},
            std::filesystem::absolute("input-dir").string());
  }
  return Finish(input);
}

namespace {

enum merge_case { MERGE_TEMPERATURE, MERGE_READING, MERGE_BAD_BYTES, MERGE_EMPTY };

auto MergeInput(merge_case scenario, int mode) -> tsugumi_bytes {
  CardInputT input;
  Decode<CardInput>(make_input(VALID, mode, -1)).UnPackTo(&input);
  using namespace inputs::arrival::tsugumi::merge;
  MergedT merged;
  if (scenario == MERGE_TEMPERATURE || scenario == MERGE_BAD_BYTES) {
    t0::TemperatureT temperature;
    temperature.value = 20.0;
    TemperatureBoxT box;
    box.bytes = scenario == MERGE_BAD_BYTES
                    ? std::vector<uint8_t>{1, 2, 3}
                    : Encode<t0::Temperature>(temperature);
    merged.value.Set(std::move(box));
  }
  if (scenario == MERGE_READING) {
    t1::ReadingT reading;
    reading.name = "sensor";
    ReadingBoxT box;
    box.bytes = Encode<t1::Reading>(reading);
    merged.value.Set(std::move(box));
  }
  AddPort(input, "arrival", PortKind::FlatBuffer, Encode<Merged>(merged));
  return Finish(input);
}

auto CheckMerge(tsugumi_bytes result, merge_case scenario) -> void {
  if (scenario == MERGE_BAD_BYTES || scenario == MERGE_EMPTY) {
    CHECK(Failure(result, FailureKind::invalid_request).message()->str().find("arrival") !=
          std::string::npos);
    return;
  }
  const auto& decoded = Decode<CardResult>(result);
  CHECK(!decoded.failure() && decoded.outputs() && decoded.outputs()->size() == 4);
  using namespace outputs::arrival::tsugumi::merge;
  ReadPort<Merged>(*decoded.outputs()->Get(3), "arrival", PortKind::FlatBuffer,
                   [scenario](const Merged& merged) {
                     const auto* temperature = merged.value_as_TemperatureBox();
                     const auto* reading = merged.value_as_ReadingBox();
                     if (scenario == MERGE_TEMPERATURE) {
                       CHECK(temperature && temperature->bytes_nested_root()->value() == 21.0);
                     } else {
                       CHECK(reading && reading->bytes_nested_root()->name()->str() == "sensor");
                     }
                   });
}

}  // namespace

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
  ReadPort<outputs::number::Number>(*ports.Get(0), "number",
                                    PortKind::FlatBuffer,
                                    [expected_number](const auto& number) {
                                      CHECK(number.value() == expected_number);
                                    });
  ReadPort<outputs::out::Sample>(
      *ports.Get(1), "out", PortKind::FlatBuffer, [](const auto& sample) {
        CHECK(sample.name()->string_view() == "table payload");
        CHECK(sample.value_as_Label() &&
              sample.value_as_Label()->value()->string_view() ==
                  "union result");
      });

  const auto& file_port = *ports.Get(2);
  CHECK(file_port.name()->string_view() == "file" &&
        file_port.kind() == PortKind::Path && !file_port.bytes());
  const auto file = file_port.path()->str();
  if (!optional_ports) {
    CHECK(file == "created.txt" && std::filesystem::is_regular_file(file));
    std::string contents;
    std::ifstream stream(file);
    CHECK(std::getline(stream, contents) && contents == "table payload");
  } else {
    CHECK(file == std::filesystem::absolute("input.txt").string());
    const auto& directory = *ports.Get(3);
    CHECK(directory.name()->string_view() == "directory" &&
          directory.kind() == PortKind::Path && !directory.bytes());
    CHECK(directory.path()->str() ==
          std::filesystem::absolute("input-dir").string());
  }
}

extern "C" auto check_input_failure(tsugumi_bytes result, input_case scenario)
    -> void {
  if (scenario == MISSING_INPUT) {
    CHECK(Failure(result, FailureKind::input_missing).message()->str() ==
          "Missing input number");
  } else {
    Failure(result, FailureKind::invalid_request);
  }
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

extern "C" auto check_display(const char* view, tsugumi_bytes bytes,
                              unsigned index) -> void {
  struct Sample {
    std::string_view view;
    std::string_view config;
    std::vector<uint8_t> data = {};
  };
  const std::array samples{
      Sample{"text", R"({"text":"résumé\n\""})"},
      Sample{"markdown", R"({"text":"**text**"})"},
      Sample{"markdown", R"({"text":"2.5"})"},
      Sample{"table", R"({"columns":["n"],"rows":[["1"]]})"},
      Sample{"image",
             R"({"alt":"sample","media_type":"image/png"})",
             {0, 13, 255}},
      Sample{"points",
             R"({"count":1})",
             {0, 0, 128, 63, 0, 0, 0, 64, 0, 0, 0, 191, 86, 52, 18, 0}},
      Sample{"line-chart",
             R"({"series":[{"count":1,"id":"id","label":"line","offset":0}]})",
             {0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 224, 191}},
      Sample{"plotly", R"({"data":[]})"},
      Sample{"vega-lite", R"({"mark":"point"})"},
      Sample{"custom", {}, {0, 13, 255}},
      Sample{"text", R"({"text":"outside"})"},
  };
  CHECK(index < samples.size());
  const auto& sample = samples[index];
  CHECK(view == sample.view);
  auto payload = std::span(bytes.data, bytes.size);
  if (sample.view != "custom") {
    const auto config = ReadDisplayConfig(payload, sample.data.size() != 0);
    CHECK(config == sample.config);
  }
  CHECK(std::ranges::equal(payload, sample.data));
}

auto main(int argc, char** argv) -> int {
  CHECK(argc == 3);
  auto* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  CHECK(library);
  CHECK(check_shared_abi(argc, argv) == 0);
  auto* diagnostics = std::getenv("DOTNET_EnableDiagnostics");
  CHECK(diagnostics && std::string_view(diagnostics) == "1");

  auto entry =
      reinterpret_cast<tsugumi_main_fn>(dlsym(library, "tsugumi_main"));
  CHECK(entry);
  tsugumi_call call{};
  call.abi_version = TSUGUMI_ABI_VERSION;
  tsugumi_bytes result{};
  for (int mode = 8; mode <= 12; ++mode) {
    call.input = make_input(VALID, mode, -1);
    CHECK(entry(&call, &result) == 0);
    switch (mode) {
      case 8:
        check_success(result, 0);
        break;
      case 9:
        Failure(result, FailureKind::invalid_request);
        break;
      case 10: {
        const auto& decoded = Decode<CardResult>(result);
        CHECK(!decoded.failure() && decoded.outputs() &&
              decoded.outputs()->size() == 4);
        const auto& port = *decoded.outputs()->Get(3);
        CHECK(port.name()->string_view() == "pack" &&
              port.kind() == PortKind::FlatBuffer);
        const std::vector<uint8_t> bytes(port.bytes()->begin(),
                                         port.bytes()->end());
        const auto& pack =
            Decode<outputs::pack::Pack>({bytes.data(), bytes.size()});
        CHECK(pack.items()->size() == 2);
        CheckLateVtable(bytes, pack.items()->Get(0));
        CHECK(pack.items()->Get(0)->text()->string_view() == "late output");
        CHECK(pack.items()->Get(1)->text()->string_view() ==
              std::string(40 * 1024, 'o'));
        break;
      }
      case 11:
      case 12:
        CHECK(Failure(result, FailureKind::worker_failed).message()->str() ==
              (mode == 11 ? "unspecified kind" : "unknown kind"));
        break;
    }
  }
  for (int scenario = MERGE_TEMPERATURE; scenario <= MERGE_EMPTY; ++scenario) {
    call.input = MergeInput(static_cast<merge_case>(scenario), 0);
    CHECK(entry(&call, &result) == 0);
    CheckMerge(result, static_cast<merge_case>(scenario));
  }
  call.input = MergeInput(MERGE_READING, 21);
  CHECK(entry(&call, &result) == 0);
  CHECK(Failure(result, FailureKind::output_invalid).message()->str().find("arrival") !=
        std::string::npos);
  call.input = make_input(VALID, 20, -1);
  CHECK(entry(&call, &result) == 0);
  CHECK(Failure(result, FailureKind::input_missing).message()->str() ==
        "Missing input unwired");
  call.input = make_input(VALID, 22, -1);
  CHECK(entry(&call, &result) == 0);
  Failure(result, FailureKind::invalid_request);
  CHECK(dlclose(library) == 0);
  puts("C# ABI 9 regression checks passed");
}
