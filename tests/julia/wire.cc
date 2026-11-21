#include "wire.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "_params_generated.h"
#include "adapter.hh"

namespace {

using namespace Tsugumi::Card;
using tsugumi::abi9::Buffer;
using Tsugumi::Data::FailureKind;
using Tsugumi::Data::PortKind;

flatbuffers::DetachedBuffer packet;

template <class Table>
auto Pack(const typename Table::NativeTableType& value)
    -> std::vector<uint8_t> {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Table::Pack(builder, &value));
  return {builder.GetBufferPointer(),
          builder.GetBufferPointer() + builder.GetSize()};
}

auto Add(CardInputT& input, const char* name, PortKind kind,
         std::vector<uint8_t> bytes) -> void {
  auto port = std::make_unique<CardPortT>();
  port->name = name;
  port->kind = kind;
  port->bytes = std::move(bytes);
  input.inputs.push_back(std::move(port));
}

template <class Table>
auto Unpack(const CardPort& port) -> typename Table::NativeTableType {
  const Buffer buffer({port.bytes()->data(), port.bytes()->size()});
  const auto* table = buffer.Verify<Table>();
  CHECK(table);
  typename Table::NativeTableType value;
  table->UnPackTo(&value);
  return value;
}

// The outer payload from the SDK is aligned for the duration of the call.
template <class Table>
auto Wire(tsugumi_bytes bytes) -> const Table& {
  CHECK(bytes.data && bytes.size);
  flatbuffers::Verifier verifier(bytes.data, bytes.size);
  CHECK(verifier.VerifyBuffer<Table>());
  return *flatbuffers::GetRoot<Table>(bytes.data);
}

auto Failure(tsugumi_bytes result, FailureKind kind) -> const CardFailure& {
  const auto& decoded = Wire<CardResult>(result);
  CHECK(decoded.failure() && decoded.failure()->kind() == kind);
  CHECK(decoded.failure()->message() && decoded.failure()->message()->size());
  CHECK(!decoded.outputs() || !decoded.outputs()->size());
  return *decoded.failure();
}

auto Read(const std::filesystem::path& path) -> std::string {
  std::ifstream stream(path);
  CHECK(stream.is_open());
  return {std::istreambuf_iterator<char>(stream), {}};
}

auto InputFile() -> std::string {
  return std::filesystem::absolute("input.txt").string();
}

auto TableInput() -> inputs::table::TableT {
  using namespace inputs::table;
  TableT table;
  const std::array<float, 2> xy{1.0f, 2.0f};
  for (const auto measure : {NAN + 0.0, INFINITY + 0.0, -INFINITY + 0.0}) {
    auto row = std::make_unique<RowT>();
    row->label = Label::Special;
    row->center =
        std::make_unique<Center>(flatbuffers::span<const float, 2>(xy));
    row->measure = measure;
    row->bytes = {0, 13, 255};
    table.rows.push_back(std::move(row));
  }
  TextT note;
  note.value = "日本語";
  table.rows[0]->choice.Set(std::move(note));
  table.rows[1]->choice.Set(Center(flatbuffers::span<const float, 2>(xy)));
  table.rows[1]->maybe = 0;
  table.rows[2]->choice.Set(std::string{"note"});
  return table;
}

}  // namespace

extern "C" auto make_input(input_case scenario, int mode, int fd)
    -> tsugumi_bytes {
  static constexpr uint8_t bad_bytes[] = {1, 2, 3};
  if (scenario == MALFORMED_ENVELOPE) return {bad_bytes, sizeof(bad_bytes)};
  if (scenario == VALID && mode == 0 &&
      !std::filesystem::exists("valid.marker")) {
    CHECK(!std::filesystem::exists("initialization.marker"));
    std::ofstream("valid.marker");
  }

  CardInputT input;
  parameters::ParamsT params;
  params.mode = mode;
  params.fd = fd;
  input.params = Pack<parameters::Params>(params);
  if (scenario == MALFORMED_PARAMS) input.params = {1, 2, 3};

  inputs::number::NumberT number;
  number.value = std::numeric_limits<uint64_t>::max();
  const auto number_bytes = Pack<inputs::number::Number>(number);
  if (scenario != MISSING_INPUT) {
    auto kind = PortKind::FlatBuffer;
    if (scenario == UNKNOWN_KIND) kind = static_cast<PortKind>(255);
    if (scenario == WRONG_KIND) kind = PortKind::File;
    Add(input, "number", kind,
        scenario == MALFORMED_PORT ? std::vector<uint8_t>{1, 2, 3}
                                   : number_bytes);
    if (scenario == DUPLICATE_PORT) Add(input, "number", kind, number_bytes);
    if (scenario == UNKNOWN_PORT) Add(input, "unknown", kind, number_bytes);
  }
  Add(input, "table", PortKind::FlatBuffer,
      Pack<inputs::table::Table>(TableInput()));
  if (scenario == OPTIONAL_PORTS) {
    inputs::file::FileT file;
    file.value = InputFile();
    Add(input, "file", PortKind::File, Pack<inputs::file::File>(file));
  }

  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(CardInput::Pack(builder, &input));
  packet = builder.Release();
  return {packet.data(), packet.size()};
}

extern "C" auto check_success(tsugumi_bytes result, int has_file) -> void {
  const auto& decoded = Wire<CardResult>(result);
  if (const auto* error = decoded.failure()) {
    std::fprintf(stderr, "CardFailure (%s): %s\n",
                 Tsugumi::Data::EnumNameFailureKind(error->kind()),
                 error->message()->c_str());
    if (error->source())
      std::fprintf(stderr, "  %s:%u\n", error->source()->c_str(),
                   error->line());
  }
  CHECK(!decoded.failure() && decoded.outputs());
  CHECK(decoded.outputs()->size() == (has_file ? 4u : 2u));
  const auto& number = *decoded.outputs()->Get(0);
  CHECK(number.name()->string_view() == "number");
  CHECK(number.kind() == PortKind::FlatBuffer);
  CHECK(Unpack<outputs::number::Number>(number).value ==
        std::numeric_limits<uint64_t>::max());
  const auto& table = *decoded.outputs()->Get(1);
  CHECK(table.name()->string_view() == "table");
  CHECK(table.kind() == PortKind::FlatBuffer);
  const auto rows = Unpack<outputs::table::Table>(table);
  CHECK(rows.rows.size() == 3);
  const auto& first = *rows.rows[0];
  const auto& second = *rows.rows[1];
  const auto& third = *rows.rows[2];
  CHECK(std::isnan(first.measure));
  CHECK(second.measure == std::numeric_limits<double>::infinity());
  CHECK(third.measure == -std::numeric_limits<double>::infinity());
  CHECK(first.label == outputs::table::Label::Special);
  CHECK(first.bytes == (std::vector<uint8_t>{255, 13, 0}));
  CHECK(!first.maybe && second.maybe == 0);
  CHECK(first.center->xy()->Get(0) == 1 && first.center->xy()->Get(1) == 2);
  CHECK(first.choice.AsNote()->value == "日本語");
  CHECK(second.choice.AsCoordinate()->xy()->Get(1) == 2);
  CHECK(*third.choice.AsString() == "note");

  if (has_file) {
    const auto& file = *decoded.outputs()->Get(2);
    const auto& forward = *decoded.outputs()->Get(3);
    CHECK(file.name()->string_view() == "file" &&
          file.kind() == PortKind::File);
    CHECK(forward.name()->string_view() == "forward" &&
          forward.kind() == PortKind::File);
    const auto output = Unpack<outputs::file::File>(file).value;
    CHECK(output == "output.txt");
    CHECK(std::filesystem::is_regular_file(
        std::filesystem::symlink_status(output)));
    CHECK(Read(output) == "CARD INPUT\n");
    CHECK(Unpack<outputs::forward::File>(forward).value == InputFile());
  }
  CHECK(Read("initialization.marker") == "loaded\n");
}

extern "C" auto check_input_failure(tsugumi_bytes result, input_case scenario)
    -> void {
  Failure(result, scenario == MISSING_INPUT ? FailureKind::input_missing
                                            : FailureKind::invalid_request);
}

extern "C" auto check_card_failure(tsugumi_bytes result, int mode) -> void {
  if (mode == 3) {
    const auto& failure = Failure(result, FailureKind::cancelled);
    CHECK(failure.message()->string_view() == "explicit failure");
    CHECK(failure.cell()->string_view() == "fit");
    CHECK(failure.source()->string_view() == "main.jl" && failure.line() == 9);
  } else if (mode <= 5) {
    const auto& failure = Failure(result, FailureKind::worker_failed);
    if (mode == 4) {
      CHECK(failure.message()->string_view() == "failure from included source");
      CHECK(failure.source()->string_view() == "failure.jl");
      CHECK(failure.line() == 2);
    } else {
      CHECK(failure.message()->string_view() == "Card threw");
    }
  } else {
    Failure(result, FailureKind::output_invalid);
  }
}

extern "C" auto check_display(tsugumi_bytes bytes, int record, unsigned index)
    -> void {
  constexpr std::array<std::string_view, 11> modules{
      "tsugumi:text",       "tsugumi:markdown",
      "tsugumi:scalar",     "tsugumi:table",
      "tsugumi:image",      "tsugumi:points",
      "tsugumi:line-chart", "tsugumi:plotly",
      "tsugumi:vega-lite",  "custom",
      "tsugumi:text"};
  const auto& display = Wire<CardDisplay>(bytes);
  CHECK(index < modules.size());
  CHECK(display.module_()->string_view() == modules[index]);
  CHECK(record == (index == 1 || index == 9));
  if (index == 0)
    CHECK(display.config()->string_view() == R"({"text":"résumé\n\""})");
  if (index == 3)
    CHECK(display.config()->string_view() ==
          R"({"columns":["n"],"rows":[["1"]]})");
  if (index == 9) {
    CHECK(display.config()->string_view() == R"({"key":1})");
    CHECK(display.blobs()->size() == 1);
    const auto* blob = display.blobs()->Get(0);
    CHECK(blob->name()->string_view() == "sample");
    CHECK(std::ranges::equal(*blob->bytes(), std::array{0, 13, 255}));
  }
  if (index == 10)
    CHECK(display.config()->string_view() == R"({"text":"outside"})");
}
