#include "wire.h"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>

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

auto CheckBlob(const CardDisplay& display, size_t index, std::string_view name,
               std::initializer_list<uint8_t> expected) -> void {
  const auto* blob = display.blobs()->Get(index);
  CHECK(blob->name()->string_view() == name);
  CHECK(std::ranges::equal(*blob->bytes(), expected));
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

  const auto file = ReadPort<outputs::file::File>(
      *ports.Get(2), "file", PortKind::File,
      [](const auto& value) { return value.value()->str(); });
  if (!optional_ports) {
    CHECK(file == "created.txt" && std::filesystem::is_regular_file(file));
    std::string contents;
    std::ifstream stream(file);
    CHECK(std::getline(stream, contents) && contents == "table payload");
  } else {
    CHECK(file == std::filesystem::absolute("input.txt").string());
    ReadPort<outputs::directory::Directory>(
        *ports.Get(3), "directory", PortKind::Directory,
        [](const auto& directory) {
          CHECK(directory.value()->str() ==
                std::filesystem::absolute("input-dir").string());
        });
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
  if (index == 4) CheckBlob(display, 0, "image", {0, 13, 255});
  if (index == 5) {
    CheckBlob(display, 0, "x", {0, 0, 128, 63});
    CheckBlob(display, 1, "y", {0, 0, 0, 64});
    CheckBlob(display, 2, "z", {0, 0, 0, 191});
    CheckBlob(display, 3, "colors", {86, 52, 18, 0});
  }
  if (index == 6) {
    CheckBlob(display, 0, "x", {0, 0, 0, 0, 0, 0, 0, 64});
    CheckBlob(display, 1, "y", {0, 0, 0, 0, 0, 0, 224, 191});
  }
  if (index == 9) CheckBlob(display, 0, "sample", {0, 13, 255});
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
  CHECK(dlclose(library) == 0);
  puts("C# ABI 9 regression checks passed");
}
