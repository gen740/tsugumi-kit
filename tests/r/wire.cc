#include "wire.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

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

auto AddPath(CardInputT& input, const char* name, std::string path) -> void {
  AddPort(input, name, PortKind::Path, {});
  input.inputs.back()->path = std::move(path);
}

auto AddArrival(CardInputT& input, bool temperature, bool bad_bytes = false)
    -> void {
  using namespace inputs::arrival::tsugumi::merge;
  MergedT merged;
  if (temperature) {
    t0::TemperatureT value;
    value.value = 20;
    TemperatureBoxT box;
    box.bytes = bad_bytes ? std::vector<uint8_t>{1, 2, 3}
                          : Encode<t0::Temperature>(value);
    merged.value.Set(std::move(box));
  } else {
    t1::ReadingT value;
    value.name = "sensor";
    ReadingBoxT box;
    box.bytes = Encode<t1::Reading>(value);
    merged.value.Set(std::move(box));
  }
  AddPort(input, "arrival", PortKind::FlatBuffer, Encode<Merged>(merged));
}

auto Pack(const CardInputT& input) -> tsugumi_bytes {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(CardInput::Pack(builder, &input));
  input_buffer = builder.Release();
  return {input_buffer.data(), input_buffer.size()};
}

auto Failure(tsugumi_bytes bytes, FailureKind kind) -> const CardFailure& {
  const auto& result = Decode<CardResult>(bytes);
  CHECK(result.failure() && result.failure()->kind() == kind);
  CHECK(result.failure()->message() && result.failure()->message()->size());
  CHECK(!result.outputs() || result.outputs()->size() == 0);
  return *result.failure();
}

}  // namespace

extern "C" auto make_input(input_case scenario, int mode, int fd)
    -> tsugumi_bytes {
  namespace rtest = inputs::number;
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
  if (scenario == WRONG_KIND) {
    AddPath(input, "number", std::filesystem::absolute("input.txt"));
  } else if (scenario != MISSING_INPUT) {
    const auto kind = scenario == UNKNOWN_KIND ? static_cast<PortKind>(255)
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

  inputs::table::DataTableT table;
  auto row = std::make_unique<inputs::table::RowT>();
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
  row->unit = {inputs::table::Unit::Volts};
  const std::array<int32_t, 2> coordinates{15, -14};
  const std::array units{inputs::table::Unit::Counts, inputs::table::Unit::Volts};
  const std::array<int32_t, 1> depth{10};
  row->position = std::make_unique<inputs::table::Position>(coordinates, units, depth);
  auto choice = std::make_unique<inputs::table::ChoiceT>();
  choice->name = "selected";
  row->selection.type = inputs::table::Selection::Alias;
  row->selection.value = choice.release();
  table.rows.push_back(std::move(row));
  AddPort(input, "table", PortKind::FlatBuffer,
          Encode<inputs::table::DataTable>(table));

  AddPath(input, "file", std::filesystem::absolute("input.txt"));
  if (scenario == VALID)
    AddPath(input, "directory", std::filesystem::absolute("directory"));
  AddArrival(input, scenario == VALID);
  return Pack(input);
}

extern "C" auto make_bad_merge_input() -> tsugumi_bytes {
  const auto valid = make_input(VALID, 0, -1);
  CardInputT input;
  Decode<CardInput>(valid).UnPackTo(&input);
  std::erase_if(input.inputs,
                [](const auto& port) { return port->name == "arrival"; });
  AddArrival(input, true, true);
  return Pack(input);
}

extern "C" auto check_success(tsugumi_bytes bytes, int call_number) -> void {
  namespace rtest = outputs::rtest;
  const auto& result = Decode<CardResult>(bytes);
  if (result.failure())
    fprintf(stderr, "%s\n", result.failure()->message()->c_str());
  const bool has_directory = call_number == 1;
  CHECK(!result.failure() && result.outputs() &&
        result.outputs()->size() == (has_directory ? 6u : 5u));
  const auto& number_port = *result.outputs()->Get(0);
  CHECK(number_port.name()->string_view() == "number");
  CHECK(number_port.kind() == PortKind::FlatBuffer);
  const auto number = DecodePort<rtest::Number>(number_port);
  CHECK(number.value == 41.5 + call_number &&
        number.unit == outputs::rtest::Unit::Volts);

  const auto& table_port = *result.outputs()->Get(1);
  CHECK(table_port.name()->string_view() == "table");
  CHECK(table_port.kind() == PortKind::FlatBuffer);
  const auto table = DecodePort<inputs::table::DataTable>(table_port);
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
  CHECK((row.unit == std::vector{inputs::table::Unit::Volts}));
  CHECK(row.position && row.position->coordinates()->Get(1) == -14);
  CHECK(row.position->units()->Get(1) == inputs::table::Unit::Volts);
  CHECK(row.position->depth()->Get(0) == 10);
  CHECK(row.selection.AsAlias() && row.selection.AsAlias()->name == "selected");

  const auto& file_port = *result.outputs()->Get(2);
  CHECK(file_port.name()->string_view() == "file" &&
        file_port.kind() == PortKind::Path && !file_port.bytes());
  const auto path = file_port.path()->str();
  CHECK(path == "result-" + std::to_string(call_number) + ".txt");
  CHECK(
      std::filesystem::is_regular_file(std::filesystem::symlink_status(path)));
  std::ifstream contents(path);
  std::string line;
  CHECK(std::getline(contents, line) && line == "input");
  CHECK(std::getline(contents, line) && line == std::to_string(call_number));

  const auto& forwarded = *result.outputs()->Get(3);
  CHECK(forwarded.name()->string_view() == "forwarded" &&
        forwarded.kind() == PortKind::Path);
  CHECK(forwarded.path()->str() == std::filesystem::absolute("input.txt"));
  if (has_directory) {
    const auto& directory = *result.outputs()->Get(4);
    CHECK(directory.name()->string_view() == "directory" &&
          directory.kind() == PortKind::Path);
    CHECK(directory.path()->str() == std::filesystem::absolute("directory"));
  }

  const auto& arrival = *result.outputs()->Get(has_directory ? 5 : 4);
  CHECK(arrival.name()->string_view() == "arrival" &&
        arrival.kind() == PortKind::FlatBuffer);
  using namespace outputs::arrival::tsugumi::merge;
  const auto merged = DecodePort<Merged>(arrival);
  if (has_directory) {
    const auto* box = merged.value.AsTemperatureBox();
    CHECK(box);
    CHECK(Decode<t0::Temperature>({box->bytes.data(), box->bytes.size()})
              .value() == 21);
  } else {
    const auto* box = merged.value.AsReadingBox();
    CHECK(box);
    CHECK(Decode<t1::Reading>({box->bytes.data(), box->bytes.size()})
              .name()
              ->str() == "sensor");
  }
}

extern "C" auto check_input_failure(tsugumi_bytes bytes, input_case scenario)
    -> void {
  if (scenario == MISSING_INPUT) {
    const auto& failure = Failure(bytes, FailureKind::input_missing);
    CHECK(failure.message()->string_view() == "Missing input number");
    CHECK(failure.source() && failure.source()->string_view() == "main.R");
  } else {
    const auto& failure = Failure(bytes, FailureKind::invalid_request);
    if (scenario == DUPLICATE_PORT + 1)
      CHECK(failure.message()->str().find("arrival") != std::string::npos);
  }
}

extern "C" auto check_card_failure(tsugumi_bytes bytes, int mode) -> void {
  if (mode == 3) {
    const auto& failure = Failure(bytes, FailureKind::worker_failed);
    CHECK(failure.message()->string_view() == "R card failure");
    CHECK(failure.source() && failure.source()->string_view() == "main.R");
    CHECK(failure.line() == 7u);
  } else if (mode == 7) {
    const auto& failure = Failure(bytes, FailureKind::input_missing);
    CHECK(failure.message()->string_view() == "Missing input unwired");
    CHECK(failure.source() && failure.source()->string_view() == "main.R");
  } else {
    Failure(bytes, FailureKind::output_invalid);
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
      Sample{"image", R"({"alt":"sample","media_type":"image/png"})",
             SampleBytes()},
      Sample{"points",
             R"({"count":1})",
             {0, 0, 128, 63, 0, 0, 128, 63, 0, 0, 128, 63, 86, 52, 18, 0}},
      Sample{"line-chart",
             R"({"series":[{"count":1,"id":"id","label":"line","offset":0}]})",
             {0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 64}},
      Sample{"plotly", R"({"data":[]})"},
      Sample{"vega-lite", R"({"mark":"point"})"},
      Sample{"custom", {}, SampleBytes()},
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

extern "C" auto reset_scratch() -> void {
  const auto entries = std::filesystem::directory_iterator("scratch");
  for (const auto& entry : entries) std::filesystem::remove_all(entry);
}

extern "C" auto throwing_cancelled(void*) -> int {
  throw std::runtime_error("cancellation callback failed");
}

extern "C" auto throwing_remaining_ms(void*) -> int64_t { throw 1; }
