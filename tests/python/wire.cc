#include "wire.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "_params_generated.h"
#include "adapter.hh"

namespace {

using namespace Tsugumi::Card;
using tsugumi::abi9::Buffer;
using Tsugumi::Data::FailureKind;
using Tsugumi::Data::PortKind;

flatbuffers::DetachedBuffer input_buffer;

template <class Table>
auto Encode(const typename Table::NativeTableType& value)
    -> std::vector<uint8_t> {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Table::Pack(builder, &value));
  return {builder.GetBufferPointer(),
          builder.GetBufferPointer() + builder.GetSize()};
}

template <class Table>
auto DecodePort(const CardPort& port, std::string_view name, PortKind kind) ->
    typename Table::NativeTableType {
  CHECK(port.name()->string_view() == name && port.kind() == kind);
  CHECK(port.bytes());
  const Buffer buffer({port.bytes()->data(), port.bytes()->size()});
  const auto* table = buffer.Verify<Table>();
  CHECK(table);
  typename Table::NativeTableType value;
  table->UnPackTo(&value);
  return value;
}

auto AddPort(CardInputT& input, std::string name, PortKind kind,
             std::vector<uint8_t> bytes) -> void {
  auto port = std::make_unique<CardPortT>();
  port->name = std::move(name);
  port->kind = kind;
  port->bytes = std::move(bytes);
  input.inputs.push_back(std::move(port));
}

auto CheckFailure(tsugumi_bytes bytes, FailureKind kind) -> CardFailureT {
  const Buffer buffer(bytes);
  const auto* result = buffer.Verify<CardResult>();
  CHECK(result && result->failure() && result->failure()->kind() == kind);
  CHECK(!result->outputs() || result->outputs()->size() == 0);
  CardFailureT failure;
  result->failure()->UnPackTo(&failure);
  CHECK(!failure.message.empty());
  return failure;
}

auto InputPath() -> std::string {
  return std::filesystem::absolute("../input.txt").lexically_normal().string();
}

auto CheckBlob(const CardDisplay& display, size_t i, std::string_view name,
               std::initializer_list<uint8_t> expected) -> void {
  const auto* blob = display.blobs()->Get(i);
  CHECK(blob->name()->string_view() == name);
  CHECK(blob->bytes() && std::ranges::equal(*blob->bytes(), expected));
}

}  // namespace

extern "C" auto make_input(input_case test_case, int mode, int fd)
    -> tsugumi_bytes {
  static constexpr uint8_t bad[] = {1, 2, 3};
  if (test_case == MALFORMED_ENVELOPE) return {bad, sizeof(bad)};

  CardInputT input;
  parameters::ParamsT params;
  params.mode = mode;
  params.fd = fd;
  input.params = Encode<parameters::Params>(params);
  if (test_case == MALFORMED_PARAMS) input.params = {1, 2, 3};

  if (test_case != MISSING_INPUT) {
    inputs::number::NumberT number;
    number.value = 40;
    auto value = Encode<inputs::number::Number>(number);
    const auto kind = test_case == UNKNOWN_KIND ? static_cast<PortKind>(255)
                      : test_case == WRONG_KIND ? PortKind::File
                                                : PortKind::FlatBuffer;
    AddPort(
        input, "number", kind,
        test_case == MALFORMED_PORT ? std::vector<uint8_t>{1, 2, 3} : value);
    if (test_case == DUPLICATE_PORT) AddPort(input, "number", kind, value);
    if (test_case == UNKNOWN_PORT) AddPort(input, "unknown", kind, value);
  }

  inputs::table::DataT table;
  auto row = std::make_unique<inputs::table::RowT>();
  row->name = "first";
  row->count = 1;
  table.rows.push_back(std::move(row));
  inputs::table::NoteT note;
  note.text = "sample";
  table.detail.Set(std::move(note));
  AddPort(input, "table", PortKind::FlatBuffer,
          Encode<inputs::table::Data>(table));

  if (test_case == OPTIONAL_PORTS) {
    inputs::file::FileT file;
    file.value = InputPath();
    AddPort(input, "file", PortKind::File, Encode<inputs::file::File>(file));
  }

  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(CardInput::Pack(builder, &input));
  input_buffer = builder.Release();
  return {input_buffer.data(), input_buffer.size()};
}

extern "C" auto check_success(tsugumi_bytes bytes, int optional_ports) -> void {
  const Buffer buffer(bytes);
  const auto* result = buffer.Verify<CardResult>();
  if (result && result->failure()) {
    const auto& failure = *result->failure();
    std::fprintf(stderr, "%s:%u: unexpected Card failure: %s\n",
                 failure.source() ? failure.source()->c_str() : "<unknown>",
                 failure.line(),
                 failure.message() ? failure.message()->c_str() : "");
  }
  CHECK(result && !result->failure() && result->outputs());
  const auto& ports = *result->outputs();
  CHECK(ports.size() == 3);
  const auto number = DecodePort<outputs::number::Number>(
      *ports.Get(0), "number", PortKind::FlatBuffer);
  CHECK(number.value == 42);
  const auto table = DecodePort<outputs::table::Data>(*ports.Get(1), "table",
                                                      PortKind::FlatBuffer);
  CHECK(table.rows.size() == 1 && table.rows.front()->name == "first");
  CHECK(table.rows.front()->count == 2 && table.detail.AsCount());
  CHECK(table.detail.AsCount()->value == 2);
  const auto file =
      DecodePort<outputs::file::File>(*ports.Get(2), "file", PortKind::File);
  CHECK(file.value == (optional_ports ? InputPath() : "result.txt"));
  CHECK(std::filesystem::is_regular_file(
      std::filesystem::symlink_status(file.value)));
  std::ifstream stream(file.value);
  std::string contents;
  std::getline(stream, contents);
  CHECK(contents == (optional_ports ? "sample input" : "42"));
}

extern "C" auto check_input_failure(tsugumi_bytes bytes, input_case test_case)
    -> void {
  CheckFailure(bytes, test_case == MISSING_INPUT
                          ? FailureKind::input_missing
                          : FailureKind::invalid_request);
}

extern "C" auto check_card_failure(tsugumi_bytes bytes, int mode) -> void {
  switch (mode) {
    case 3: {
      const auto failure = CheckFailure(bytes, FailureKind::cancelled);
      CHECK(failure.message == "explicit failure" && failure.cell == "fit");
      CHECK(failure.source == "src/main.py" && failure.line > 0);
      break;
    }
    case 4: {
      const auto failure = CheckFailure(bytes, FailureKind::worker_failed);
      CHECK(failure.message == "RuntimeError: standard exception");
      CHECK(failure.source == "src/main.py" && failure.line > 0);
      break;
    }
    case 5:
      CHECK(CheckFailure(bytes, FailureKind::worker_failed).message ==
            "SystemExit: 5");
      break;
    default:
      CheckFailure(bytes, FailureKind::output_invalid);
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
  const Buffer buffer(bytes);
  const auto* display = buffer.Verify<CardDisplay>();
  CHECK(display && index < samples.size());
  const auto& sample = samples[index];
  CHECK(display->module_()->string_view() == sample.module);
  CHECK(display->config()->string_view() == sample.config);
  CHECK(display->blobs() && display->blobs()->size() == sample.blobs);
  CHECK(record == sample.record);
  if (index == 4) CheckBlob(*display, 0, "image", {0, 13, 255});
  if (index == 5) {
    CheckBlob(*display, 0, "x", {0, 0, 128, 63});
    CheckBlob(*display, 1, "y", {0, 0, 128, 63});
    CheckBlob(*display, 2, "z", {0, 0, 128, 63});
    CheckBlob(*display, 3, "colors", {86, 52, 18, 0});
  }
  if (index == 6) {
    CheckBlob(*display, 0, "x", {0, 0, 0, 0, 0, 0, 0, 64});
    CheckBlob(*display, 1, "y", {0, 0, 0, 0, 0, 0, 0, 64});
  }
  if (index == 9) CheckBlob(*display, 0, "sample", {0, 13, 255});
}
