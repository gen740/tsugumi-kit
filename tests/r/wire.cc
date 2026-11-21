#include "wire.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "_params_generated.h"
#include "tsugumi/protocol/card_generated.h"

namespace {
using namespace Tsugumi::Card;
using Tsugumi::Data::FailureKind;
using Tsugumi::Data::PortKind;

flatbuffers::DetachedBuffer input_buffer;

auto SampleBytes() -> std::vector<uint8_t> {
  std::vector<uint8_t> bytes(128);
  std::iota(bytes.begin(), bytes.end(), 0);
  return bytes;
}

template <typename Table>
auto Encode(const typename Table::NativeTableType& value)
    -> std::vector<uint8_t> {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Table::Pack(builder, &value));
  return {builder.GetBufferPointer(),
          builder.GetBufferPointer() + builder.GetSize()};
}

template <typename Table>
auto Decode(tsugumi_bytes bytes) -> const Table& {
  CHECK(bytes.data && bytes.size);
  flatbuffers::Verifier verifier(bytes.data, bytes.size);
  CHECK(verifier.VerifyBuffer<Table>());
  return *flatbuffers::GetRoot<Table>(bytes.data);
}

template <typename Table>
auto DecodePort(const CardPort& port) -> typename Table::NativeTableType {
  std::vector<uint64_t> aligned((port.bytes()->size() + 7) / 8);
  std::memcpy(aligned.data(), port.bytes()->data(), port.bytes()->size());
  typename Table::NativeTableType native;
  Decode<Table>(
      {reinterpret_cast<const uint8_t*>(aligned.data()), port.bytes()->size()})
      .UnPackTo(&native);
  return native;
}

auto AddPort(CardInputT& input, const char* name, PortKind kind,
             std::vector<uint8_t> bytes) -> void {
  auto port = std::make_unique<CardPortT>();
  port->name = name;
  port->kind = kind;
  port->bytes = std::move(bytes);
  input.inputs.push_back(std::move(port));
}

auto Failure(tsugumi_bytes bytes, FailureKind kind) -> const CardFailure& {
  const auto& result = Decode<CardResult>(bytes);
  CHECK(result.failure() && result.failure()->kind() == kind);
  CHECK(result.failure()->message() && result.failure()->message()->size());
  CHECK(!result.outputs() || result.outputs()->size() == 0);
  return *result.failure();
}

auto CheckBlob(const CardDisplay& display, unsigned index,
               std::string_view name, const std::vector<uint8_t>& bytes)
    -> void {
  const auto& blob = *display.blobs()->Get(index);
  CHECK(blob.name()->string_view() == name);
  CHECK(std::ranges::equal(*blob.bytes(), bytes));
}

}  // namespace

extern "C" auto make_input(input_case scenario, int mode, int fd)
    -> tsugumi_bytes {
  static constexpr uint8_t bad[] = {1, 2, 3};
  if (scenario == MALFORMED_ENVELOPE) return {bad, sizeof(bad)};
  CardInputT input;
  parameters::ParamsT params;
  params.mode = mode;
  params.fd = fd;
  input.params = scenario == MALFORMED_PARAMS
                     ? std::vector<uint8_t>{1, 2, 3}
                     : Encode<parameters::Params>(params);
  rtest::NumberT number;
  number.value = 41.5;
  number.unit = rtest::Unit::Volts;
  if (scenario != MISSING_INPUT) {
    const auto kind = scenario == UNKNOWN_KIND ? static_cast<PortKind>(255)
                      : scenario == WRONG_KIND ? PortKind::File
                                               : PortKind::FlatBuffer;
    AddPort(input, "number", kind,
            scenario == MALFORMED_PORT ? std::vector<uint8_t>{1, 2, 3}
                                       : Encode<rtest::Number>(number));
    if (scenario == DUPLICATE_PORT)
      AddPort(input, "number", kind, Encode<rtest::Number>(number));
    if (scenario == UNKNOWN_PORT)
      AddPort(input, "unknown", kind, Encode<rtest::Number>(number));
  }

  number.value = 17;
  AddPort(input, "params", PortKind::FlatBuffer, Encode<rtest::Number>(number));
  number.value = 19;
  AddPort(input, "params_", PortKind::FlatBuffer,
          Encode<rtest::Number>(number));

  rtest::DataTableT table;
  auto row = std::make_unique<rtest::RowT>();
  row->name = "résumé";
  row->signed_value = INT64_MIN;
  row->unsigned_value = UINT64_MAX;
  row->special = {std::numeric_limits<double>::quiet_NaN(), INFINITY, -INFINITY,
                  std::nextafter(1.0, 2.0), 9007199254740992.0};
  row->single = {7};
  row->wide = {7, UINT64_MAX};
  row->bytes = SampleBytes();
  row->enabled.push_back(true);
  row->enabled.push_back(false);
  row->unit = {rtest::Unit::Volts};
  const std::array<int32_t, 2> coordinates{15, -14};
  const std::array units{rtest::Unit::Counts, rtest::Unit::Volts};
  const std::array<int32_t, 1> depth{10};
  row->position = std::make_unique<rtest::Position>(coordinates, units, depth);
  auto choice = std::make_unique<rtest::ChoiceT>();
  choice->name = "selected";
  row->selection.type = rtest::Selection::Alias;
  row->selection.value = choice.release();
  table.rows.push_back(std::move(row));
  AddPort(input, "table", PortKind::FlatBuffer,
          Encode<rtest::DataTable>(table));

  rtest::FileT file;
  file.value = std::filesystem::absolute("input.txt").string();
  AddPort(input, "file", PortKind::File, Encode<rtest::File>(file));
  if (scenario == VALID) {
    rtest::DirectoryT directory;
    directory.value = std::filesystem::absolute("directory").string();
    AddPort(input, "directory", PortKind::Directory,
            Encode<rtest::Directory>(directory));
  }
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(CardInput::Pack(builder, &input));
  input_buffer = builder.Release();
  return {input_buffer.data(), input_buffer.size()};
}

extern "C" auto check_success(tsugumi_bytes bytes, int call_number) -> void {
  const auto& result = Decode<CardResult>(bytes);
  if (result.failure())
    fprintf(stderr, "%s\n", result.failure()->message()->c_str());
  const bool has_directory = call_number == 1;
  CHECK(!result.failure() && result.outputs() &&
        result.outputs()->size() == (has_directory ? 5u : 4u));
  const auto& number_port = *result.outputs()->Get(0);
  CHECK(number_port.name()->string_view() == "number");
  CHECK(number_port.kind() == PortKind::FlatBuffer);
  const auto number = DecodePort<rtest::Number>(number_port);
  CHECK(number.value == 41.5 + call_number &&
        number.unit == rtest::Unit::Volts);

  const auto& table_port = *result.outputs()->Get(1);
  CHECK(table_port.name()->string_view() == "table");
  CHECK(table_port.kind() == PortKind::FlatBuffer);
  const auto table = DecodePort<rtest::DataTable>(table_port);
  CHECK(table.rows.size() == 1);
  const auto& row = *table.rows.front();
  CHECK(row.name == "résumé");
  CHECK(row.signed_value == INT64_MIN && row.unsigned_value == UINT64_MAX);
  CHECK(!row.nullable.has_value());
  CHECK(row.special.size() == 5 && std::isnan(row.special[0]));
  CHECK(row.special[1] == INFINITY && row.special[2] == -INFINITY);
  CHECK(row.special[3] == std::nextafter(1.0, 2.0));
  CHECK(row.special[4] == 9007199254740992.0);
  CHECK((row.single == std::vector<double>{8}));
  CHECK(row.empty.empty());
  CHECK((row.wide == std::vector<uint64_t>{7, UINT64_MAX}));
  CHECK(row.bytes == SampleBytes());
  CHECK((row.enabled == std::vector<bool>{true, false}));
  CHECK((row.unit == std::vector{rtest::Unit::Volts}));
  CHECK(row.position && row.position->coordinates()->Get(1) == -14);
  CHECK(row.position->units()->Get(1) == rtest::Unit::Volts);
  CHECK(row.position->depth()->Get(0) == 10);
  CHECK(row.selection.AsAlias() && row.selection.AsAlias()->name == "selected");

  const auto& file_port = *result.outputs()->Get(2);
  CHECK(file_port.name()->string_view() == "file" &&
        file_port.kind() == PortKind::File);
  const auto path = DecodePort<rtest::File>(file_port).value;
  CHECK(path == "result-" + std::to_string(call_number) + ".txt");
  CHECK(
      std::filesystem::is_regular_file(std::filesystem::symlink_status(path)));
  std::ifstream contents(path);
  std::string line;
  CHECK(std::getline(contents, line) && line == "input");
  CHECK(std::getline(contents, line) && line == std::to_string(call_number));

  const auto& forwarded = *result.outputs()->Get(3);
  CHECK(forwarded.name()->string_view() == "forwarded" &&
        forwarded.kind() == PortKind::File);
  CHECK(DecodePort<rtest::File>(forwarded).value ==
        std::filesystem::absolute("input.txt"));
  if (has_directory) {
    const auto& directory = *result.outputs()->Get(4);
    CHECK(directory.name()->string_view() == "directory" &&
          directory.kind() == PortKind::Directory);
    CHECK(DecodePort<rtest::Directory>(directory).value ==
          std::filesystem::absolute("directory"));
  }
}

extern "C" auto check_input_failure(tsugumi_bytes bytes, input_case scenario)
    -> void {
  Failure(bytes, scenario == MISSING_INPUT ? FailureKind::input_missing
                                           : FailureKind::invalid_request);
}

extern "C" auto check_card_failure(tsugumi_bytes bytes, int mode) -> void {
  if (mode == 3) {
    const auto& failure = Failure(bytes, FailureKind::worker_failed);
    CHECK(failure.message()->string_view() == "R card failure");
    CHECK(failure.source() && failure.source()->string_view() == "main.R");
    CHECK(failure.line() == 7u);
  } else {
    Failure(bytes, FailureKind::output_invalid);
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
  if (display.config()->string_view() != sample.config)
    fprintf(stderr, "Display %u config: %s\n", index,
            display.config()->c_str());
  CHECK(display.config()->string_view() == sample.config);
  CHECK(display.blobs() && display.blobs()->size() == sample.blobs);
  CHECK(record == sample.record);
  if (index == 4) CheckBlob(display, 0, "image", SampleBytes());
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
  if (index == 9) CheckBlob(display, 0, "sample", SampleBytes());
}

extern "C" auto reset_scratch() -> void {
  const auto entries = std::filesystem::directory_iterator("scratch");
  for (const auto& entry : entries) std::filesystem::remove_all(entry);
}

extern "C" auto throwing_cancelled(void*) -> int {
  throw std::runtime_error("cancellation callback failed");
}

extern "C" auto throwing_remaining_ms(void*) -> int64_t { throw 1; }
