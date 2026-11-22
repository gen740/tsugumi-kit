#include "wire.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "_params_generated.h"
#include "adapter.hh"

namespace {

using namespace Tsugumi::Card;
using tsugumi::abi::Buffer;
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

auto AddPath(CardInputT& input, std::string name, std::string path) -> void {
  AddPort(input, std::move(name), PortKind::Path, {});
  input.inputs.back()->path = std::move(path);
}

auto CheckPath(const CardPort& port, std::string_view name) -> std::string {
  CHECK(port.name()->string_view() == name && port.kind() == PortKind::Path);
  CHECK(port.path() && (!port.bytes() || port.bytes()->size() == 0));
  return port.path()->str();
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

auto DirectoryPath() -> std::string {
  return std::filesystem::absolute("../input dir").lexically_normal().string() +
         "/.";
}

}  // namespace

auto MakeInput(input_case test_case, int mode, int fd, int ack_fd)
    -> tsugumi_bytes {
  static constexpr uint8_t bad[] = {1, 2, 3};
  if (test_case == MALFORMED_ENVELOPE) return {bad, sizeof(bad)};

  CardInputT input;
  parameters::ParamsT params;
  params.mode = mode;
  params.fd = fd;
  params.ack_fd = ack_fd;
  input.params = Encode<parameters::Params>(params);
  if (test_case == MALFORMED_PARAMS) input.params = {1, 2, 3};

  if (test_case != MISSING_INPUT) {
    inputs::from::NumberT number;
    number.position = std::make_unique<inputs::from::Position>(39, -200);
    auto value = Encode<inputs::from::Number>(number);
    const auto kind = test_case == UNKNOWN_KIND ? static_cast<PortKind>(255)
                      : test_case == WRONG_KIND ? PortKind::Path
                                                : PortKind::FlatBuffer;
    AddPort(
        input, "from", kind,
        test_case == MALFORMED_PORT ? std::vector<uint8_t>{1, 2, 3} : value);
    if (test_case == DUPLICATE_PORT) AddPort(input, "from", kind, value);
    if (test_case == UNKNOWN_PORT) AddPort(input, "unknown", kind, value);
  }
  inputs::from_::NumberT extra_number;
  extra_number.value = 1;
  AddPort(input, "from_", PortKind::FlatBuffer,
          Encode<inputs::from_::Number>(extra_number));

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
    AddPath(input, "file", InputPath());
    AddPath(input, "directory", DirectoryPath());
  }

  if (test_case == OPTIONAL_PORTS || mode == 14 || mode == 15 || mode == 21) {
    using namespace inputs::arrival::tsugumi::merge;
    MergedT merged;
    if (mode == 15) {
      t1::ReadingT reading;
      reading.name = "barometer";
      ReadingBoxT box;
      box.bytes = Encode<t1::Reading>(reading);
      merged.value.Set(std::move(box));
    } else if (mode != 21) {
      t0::TemperatureT temperature;
      temperature.value = 24.0;
      TemperatureBoxT box;
      box.bytes = mode == 14 ? std::vector<uint8_t>{1, 2, 3}
                             : Encode<t0::Temperature>(temperature);
      merged.value.Set(std::move(box));
    }
    AddPort(input, "arrival", PortKind::FlatBuffer, Encode<Merged>(merged));
  }

  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(CardInput::Pack(builder, &input));
  input_buffer = builder.Release();
  return {input_buffer.data(), input_buffer.size()};
}

extern "C" auto make_input(input_case test_case, int mode, int fd)
    -> tsugumi_bytes {
  return MakeInput(test_case, mode, fd, -1);
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
  CHECK(ports.size() == (optional_ports == 1   ? 5
                         : optional_ports == 2 ? 4
                                               : 3));
  const auto number = DecodePort<outputs::number::Number>(
      *ports.Get(0), "number", PortKind::FlatBuffer);
  CHECK(number.value == 42);
  CHECK(number.position && number.position->x() == 39 &&
        number.position->y() == -200);
  const auto table = DecodePort<outputs::table::Data>(*ports.Get(1), "table",
                                                      PortKind::FlatBuffer);
  CHECK(table.rows.size() == 1 && table.rows.front()->name == "first");
  CHECK(table.rows.front()->count == 2 && table.detail.AsCount());
  CHECK(table.detail.AsCount()->value == 2);
  CHECK(table.rows.front()->row_detail.AsCount() &&
        table.rows.front()->row_detail.AsCount()->value == 2);
  const auto file = CheckPath(*ports.Get(2), "file");
  CHECK(file == (optional_ports == 1 ? InputPath() : "result.txt"));
  CHECK(
      std::filesystem::is_regular_file(std::filesystem::symlink_status(file)));
  std::ifstream stream(file);
  std::string contents;
  std::getline(stream, contents);
  CHECK(contents == (optional_ports == 1 ? "sample input" : "42"));
  using namespace outputs::arrival::tsugumi::merge;
  if (optional_ports == 1) {
    const auto directory = CheckPath(*ports.Get(3), "with");
    CHECK(directory == DirectoryPath());
    std::ifstream sample(std::filesystem::path(directory) / "sample.txt");
    std::getline(sample, contents);
    CHECK(contents == "sample directory");
    const auto merged =
        DecodePort<Merged>(*ports.Get(4), "arrival", PortKind::FlatBuffer);
    CHECK(merged.value.AsTemperatureBox());
    const auto& bytes = merged.value.AsTemperatureBox()->bytes;
    const Buffer nested({bytes.data(), bytes.size()});
    const auto* temperature = nested.Verify<t0::Temperature>();
    CHECK(temperature && temperature->value() == 25.0);
  } else if (optional_ports == 2) {
    const auto merged =
        DecodePort<Merged>(*ports.Get(3), "arrival", PortKind::FlatBuffer);
    CHECK(merged.value.AsReadingBox());
    const auto& bytes = merged.value.AsReadingBox()->bytes;
    const Buffer nested({bytes.data(), bytes.size()});
    const auto* reading = nested.Verify<t1::Reading>();
    CHECK(reading && reading->name()->string_view() == "barometer");
  }
}

extern "C" auto check_input_failure(tsugumi_bytes bytes, input_case test_case)
    -> void {
  if (test_case == MISSING_INPUT)
    CHECK(CheckFailure(bytes, FailureKind::input_missing).message ==
          "Missing input from");
  else
    CheckFailure(bytes, FailureKind::invalid_request);
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
    case 8:
    case 9:
    case 10: {
      const auto failure = CheckFailure(bytes, FailureKind::output_invalid);
      CHECK(failure.message.find("does not match its union tag") !=
            std::string::npos);
      break;
    }
    case 13:
      CHECK(CheckFailure(bytes, FailureKind::input_missing).message ==
            "Missing input unwired");
      break;
    case 14:
    case 21:
      CHECK(CheckFailure(bytes, FailureKind::invalid_request)
                .message.find("arrival") != std::string::npos);
      break;
    default:
      CheckFailure(bytes, FailureKind::output_invalid);
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
             {0, 0, 128, 63, 0, 0, 0, 64, 0, 0, 64, 64}},
      Sample{"line-chart",
             R"({"series":[{"count":1,"id":"id","label":"line","offset":0}]})",
             {0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 8, 64}},
      Sample{"plotly", R"({"data":[]})"},
      Sample{
          "plotly",
          R"({"data":[{"type":"scatter","x":["2026-11-22"],"y":[2]}],"layout":{}})"},
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
