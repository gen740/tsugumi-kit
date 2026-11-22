#include "wire.h"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <vector>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "_params_generated.h"
#include "adapter.hh"
#include "tsugumi/protocol/card_generated.h"

extern "C" int shared_fake_host_main(int argc, char** argv);
extern "C" char** environ;

namespace {

using namespace Tsugumi::Card;
using nlohmann::json;
using Tsugumi::Data::FailureKind;
using Tsugumi::Data::PortKind;

flatbuffers::DetachedBuffer input_buffer;

template <class Table>
auto Read(tsugumi_bytes bytes) -> const Table& {
  CHECK(bytes.data && bytes.size);
  flatbuffers::Verifier verifier(bytes.data, bytes.size);
  CHECK(verifier.VerifyBuffer<Table>());
  return *flatbuffers::GetRoot<Table>(bytes.data);
}

template <class Table>
auto ReadPort(const CardPort& port) -> typename Table::NativeTableType {
  CHECK(port.bytes());
  return tsugumi::abi::ReadValue<Table>(
      {port.bytes()->data(), port.bytes()->size()}, port.name()->string_view());
}

template <class Table>
auto Encode(const typename Table::NativeTableType& value)
    -> std::vector<uint8_t> {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Table::Pack(builder, &value));
  return {builder.GetBufferPointer(),
          builder.GetBufferPointer() + builder.GetSize()};
}

auto AddPort(CardInputT& input, const char* name, std::vector<uint8_t> bytes,
             PortKind kind = PortKind::FlatBuffer) -> void {
  auto port = std::make_unique<CardPortT>();
  port->name = name;
  port->kind = kind;
  port->bytes = std::move(bytes);
  input.inputs.push_back(std::move(port));
}

auto AddPath(CardInputT& input, const char* name, std::string path) -> void {
  AddPort(input, name, {}, PortKind::Path);
  input.inputs.back()->path = std::move(path);
}

// Extra inputs of the Ruby-specific cases, added to the next make_input.
enum class Extra {
  none,
  keyword,
  temperature,
  reading,
  bad_box,
  empty_merge
};
Extra extra = Extra::none;

auto AddExtra(CardInputT& input) -> void {
  using namespace inputs::arrival::tsugumi::merge;
  if (extra == Extra::none) return;
  if (extra == Extra::keyword) {
    using Keyword = inputs::tsugumi_class::tsugumi_template::tsugumi_union;
    Keyword::NativeTableType value;
    value.value = 43;
    AddPort(input, "class", Encode<Keyword>(value));
    return;
  }
  MergedT merged;
  if (extra == Extra::temperature || extra == Extra::bad_box) {
    t0::TemperatureT temperature;
    temperature.value = 20.0;
    TemperatureBoxT box;
    box.bytes = extra == Extra::bad_box ? std::vector<uint8_t>{1, 2, 3}
                                        : Encode<t0::Temperature>(temperature);
    merged.value.Set(std::move(box));
  } else if (extra == Extra::reading) {
    t1::ReadingT reading;
    reading.name = "sensor";
    ReadingBoxT box;
    box.bytes = Encode<t1::Reading>(reading);
    merged.value.Set(std::move(box));
  }
  AddPort(input, "arrival", Encode<Merged>(merged));
}

constexpr int kDeepTreeLevels = 50;

auto Rows(int tree_levels) -> inputs::rows::RowsT {
  using namespace inputs::rows;
  auto row = std::make_unique<RowT>();
  row->label = "sample";
  row->magnitude = 2.5;
  row->bytes = {0, 255};
  row->tone = Tone::delete_;
  row->optional = 7;
  const std::array point{1.5, 2.5};
  const std::array<uint8_t, 4> flags{1, 0, 0, 1};
  row->position = std::make_unique<Position>(
      flatbuffers::span<const double, 2>(point.data(), point.size()),
      flatbuffers::span<const uint8_t, 4>(flags.data(), flags.size()));
  row->choice.Set(WordT{});
  row->choice.AsWord()->text = "nested";
  row->history.emplace_back().Set(CountT{});
  row->history.front().AsCount()->value = 17;
  row->history.front().AsCount()->default_ = 5;
  row->history.emplace_back().Set(WordT{});
  row->history.back().AsWord()->text = "history";
  row->readings = {std::numeric_limits<double>::quiet_NaN(),
                   std::numeric_limits<double>::infinity(),
                   -std::numeric_limits<double>::infinity()};
  for (int value = 1; value <= tree_levels; ++value) {
    auto parent = std::make_unique<NodeT>();
    parent->value = value;
    if (row->tree) parent->children.push_back(std::move(row->tree));
    row->tree = std::move(parent);
  }
  RowsT result;
  result.rows.push_back(std::move(row));
  return result;
}

auto Failure(tsugumi_bytes bytes, FailureKind kind) -> const CardFailure& {
  const auto& result = Read<CardResult>(bytes);
  CHECK(result.failure() && result.failure()->kind() == kind);
  CHECK(result.failure()->message() && result.failure()->message()->size());
  CHECK(!result.outputs() || result.outputs()->size() == 0);
  return *result.failure();
}

auto InputPath() -> std::string {
  return std::filesystem::absolute("ruby-input.data").string();
}

auto DirectoryPath() -> std::string {
  return std::filesystem::absolute("ruby-input-dir").string();
}

auto Environment() -> std::set<std::string> {
  std::set<std::string> environment;
  for (auto** entry = environ; *entry; ++entry) environment.emplace(*entry);
  return environment;
}

}  // namespace

extern "C" auto make_input(input_case scenario, int mode, int fd)
    -> tsugumi_bytes {
  static constexpr uint8_t bad_bytes[]{1, 2, 3};
  if (scenario == MALFORMED_ENVELOPE) return {bad_bytes, sizeof(bad_bytes)};

  CardInputT input;
  parameters::ParamsT params;
  params.mode = mode;
  params.fd = fd;
  input.params = Encode<parameters::Params>(params);
  if (scenario == MALFORMED_PARAMS) input.params = {1, 2, 3};
  if (scenario != MISSING_INPUT) {
    inputs::number::NumberT number;
    number.value = 40;
    const auto bytes = scenario == MALFORMED_PORT
                           ? std::vector<uint8_t>{1, 2, 3}
                           : Encode<inputs::number::Number>(number);
    if (scenario == WRONG_KIND) {
      AddPath(input, "number", InputPath());
    } else {
      AddPort(input, "number", bytes,
              scenario == UNKNOWN_KIND ? static_cast<PortKind>(255)
                                       : PortKind::FlatBuffer);
    }
    if (scenario == DUPLICATE_PORT) AddPort(input, "number", bytes);
    if (scenario == UNKNOWN_PORT) AddPort(input, "unknown", bytes);
  }
  const int tree_levels = mode == 12 ? kDeepTreeLevels : 0;
  AddPort(input, "rows", Encode<inputs::rows::Rows>(Rows(tree_levels)));
  if (scenario == OPTIONAL_PORTS) {
    std::ofstream(InputPath()) << "from ruby";
    AddPath(input, "file", InputPath());
    std::filesystem::create_directories(DirectoryPath());
    std::ofstream(std::filesystem::path(DirectoryPath()) / "source") << "input";
    AddPath(input, "directory", DirectoryPath());
  }
  AddExtra(input);

  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(CardInput::Pack(builder, &input));
  input_buffer = builder.Release();
  return {input_buffer.data(), input_buffer.size()};
}

namespace {

auto CheckSuccess(tsugumi_bytes bytes, bool optional_ports, int tree_levels)
    -> void {
  const auto& result = Read<CardResult>(bytes);
  if (result.failure())
    std::fprintf(stderr, "Ruby Card failure: %s\n",
                 result.failure()->message()->c_str());
  CHECK(!result.failure() && result.outputs());
  const auto extras = extra == Extra::none ? 0u : 1u;
  CHECK(result.outputs()->size() == (optional_ports ? 5u : 2u) + extras);
  const auto& number = *result.outputs()->Get(0);
  CHECK(number.name()->string_view() == "number" &&
        number.kind() == PortKind::FlatBuffer);
  CHECK(ReadPort<outputs::number::Number>(number).value == 42);

  const auto& table = *result.outputs()->Get(1);
  CHECK(table.name()->string_view() == "rows" &&
        table.kind() == PortKind::FlatBuffer);
  const auto rows = ReadPort<outputs::rows::Rows>(table);
  CHECK(rows.rows.size() == 1);
  const auto& row = *rows.rows.front();
  CHECK(row.label == "sample" && row.magnitude == 2.5);
  CHECK(row.tone == outputs::rows::Tone::delete_ && row.optional == 7);
  CHECK(row.position->point()->Get(0) == 1.5);
  CHECK(row.position->point()->Get(1) == 2.5);
  CHECK(std::ranges::equal(*row.position->flags(), std::array{0, 0, 1, 1}));
  CHECK((row.bytes == std::vector<uint8_t>{0, 13, 255}));
  CHECK(row.readings.size() == 3 && std::isnan(row.readings[0]));
  CHECK(row.readings[1] == std::numeric_limits<double>::infinity());
  CHECK(row.readings[2] == -std::numeric_limits<double>::infinity());
  CHECK(row.choice.AsCount() && row.choice.AsCount()->value == 12);
  CHECK(row.choice.AsCount()->default_ == 3 &&
        row.choice.AsCount()->tsugumi_hidden == 4);
  CHECK(row.history.size() == 2);
  CHECK(row.history[0].AsCount() && row.history[0].AsCount()->value == 17);
  CHECK(row.history[1].AsWord() && row.history[1].AsWord()->text == "history");

  const auto* node = row.tree.get();
  for (int value = tree_levels; value > 0; --value) {
    CHECK(node && node->value == value);
    CHECK(node->children.size() == (value > 1 ? 1u : 0u));
    node = value > 1 ? node->children.front().get() : nullptr;
  }
  CHECK(!node);

  if (optional_ports) {
    const auto& file = *result.outputs()->Get(2);
    CHECK(file.name()->string_view() == "file" &&
          file.kind() == PortKind::Path && !file.bytes());
    const auto path = file.path()->str();
    CHECK(path == "ruby-output.data");
    CHECK(std::filesystem::is_regular_file(
        std::filesystem::symlink_status(path)));
    std::ifstream stream(path);
    std::string contents;
    CHECK(std::getline(stream, contents) && contents == "FROM RUBY");

    const auto& original = *result.outputs()->Get(3);
    CHECK(original.name()->string_view() == "original" &&
          original.kind() == PortKind::Path && !original.bytes());
    CHECK(original.path()->str() == InputPath());

    const auto& directory = *result.outputs()->Get(4);
    CHECK(directory.name()->string_view() == "directory" &&
          directory.kind() == PortKind::Path && !directory.bytes());
    CHECK(directory.path()->str() == DirectoryPath());
  }
  if (extra == Extra::none) return;
  const auto& port = *result.outputs()->Get(result.outputs()->size() - 1);
  CHECK(port.kind() == PortKind::FlatBuffer);
  if (extra == Extra::keyword) {
    using Keyword = outputs::tsugumi_class::tsugumi_template::tsugumi_union;
    CHECK(port.name()->string_view() == "switch");
    CHECK(ReadPort<Keyword>(port).value == 44);
    return;
  }
  using namespace outputs::arrival::tsugumi::merge;
  CHECK(port.name()->string_view() == "arrival");
  const auto merged = ReadPort<Merged>(port);
  if (extra == Extra::temperature) {
    const auto* box = merged.value.AsTemperatureBox();
    CHECK(box);
    CHECK(tsugumi::abi::ReadValue<t0::Temperature>(
              {box->bytes.data(), box->bytes.size()}, "arrival")
              .value == 21.0);
  } else {
    const auto* box = merged.value.AsReadingBox();
    CHECK(box);
    CHECK(tsugumi::abi::ReadValue<t1::Reading>(
              {box->bytes.data(), box->bytes.size()}, "arrival")
              .name == "sensor");
  }
}

}  // namespace

extern "C" auto check_success(tsugumi_bytes bytes, int optional_ports) -> void {
  CheckSuccess(bytes, optional_ports != 0, 0);
}

extern "C" auto check_input_failure(tsugumi_bytes bytes, input_case scenario)
    -> void {
  if (scenario == MISSING_INPUT) {
    const auto& failure = Failure(bytes, FailureKind::input_missing);
    CHECK(failure.message()->string_view() == "Missing input number");
    CHECK(failure.source() &&
          failure.source()->string_view() == "src/main.rb");
  } else {
    Failure(bytes, FailureKind::invalid_request);
  }
}

extern "C" auto check_card_failure(tsugumi_bytes bytes, int mode) -> void {
  switch (mode) {
    case 3: {
      const auto& failure = Failure(bytes, FailureKind::cancelled);
      CHECK(failure.message()->string_view() == "explicit failure");
      CHECK(failure.source() &&
            failure.source()->string_view() == "src/main.rb");
      CHECK(failure.line() > 0);
      CHECK(failure.cell() && failure.cell()->string_view() == "fit");
      break;
    }
    case 4:
      CHECK(
          Failure(bytes, FailureKind::worker_failed).message()->string_view() ==
          "RuntimeError: standard exception");
      break;
    case 5:
      Failure(bytes, FailureKind::worker_failed);
      break;
    case 14:
      CHECK(
          Failure(bytes, FailureKind::input_missing).message()->string_view() ==
          "Missing input unwired");
      break;
    default:
      Failure(bytes, FailureKind::output_invalid);
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
             {0, 0, 128, 63, 0, 0, 0, 192, 0, 0, 96, 64, 86, 52, 18, 0}},
      Sample{"line-chart",
             R"({"series":[{"count":1,"id":"id","label":"line","offset":0}]})",
             {0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 12, 192}},
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
    CHECK(json::parse(config) == json::parse(sample.config));
  }
  CHECK(std::ranges::equal(payload, sample.data));
}

extern "C" auto throwing_cancelled(void*) -> int {
  throw std::runtime_error("cancelled from the host");
}

extern "C" auto throwing_remaining_ms(void*) -> int64_t { throw 42; }

auto main(int argc, char** argv) -> int {
  CHECK(argc == 3);
  void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  CHECK(library);
  // On macOS dlopen initializes CoreFoundation, which may add its own
  // encoding variable before the first Card call.
  const auto environment = Environment();
  CHECK(shared_fake_host_main(argc, argv) == 0);
  CHECK(Environment() == environment);
  const auto entry =
      reinterpret_cast<tsugumi_main_fn>(dlsym(library, "tsugumi_main"));
  CHECK(entry);
  const tsugumi_effects effects{nullptr, throwing_cancelled,
                                throwing_remaining_ms};
  tsugumi_call call{TSUGUMI_ABI_VERSION, {}, nullptr, &effects};
  tsugumi_bytes result;

  call.input = make_input(VALID, 8, -1);
  CHECK(entry(&call, &result) == 0);
  Failure(result, FailureKind::output_invalid);

  call.input = make_input(VALID, 9, -1);
  CHECK(entry(&call, &result) == 0);
  CHECK(Failure(result, FailureKind::worker_failed).message()->string_view() ==
        "RuntimeError: cancelled callback failed");

  call.input = make_input(VALID, 10, -1);
  CHECK(entry(&call, &result) == 0);
  CHECK(Failure(result, FailureKind::worker_failed).message()->string_view() ==
        "RuntimeError: remaining_ms callback failed");

  call.input = make_input(VALID, 11, -1);
  CHECK(entry(&call, &result) == 0);
  CheckSuccess(result, false, 0);
  CHECK(Environment() == environment);

  call.input = make_input(VALID, 12, -1);
  CHECK(entry(&call, &result) == 0);
  CheckSuccess(result, false, kDeepTreeLevels);

  call.input = make_input(VALID, 13, -1);
  CHECK(entry(&call, &result) == 0);
  Failure(result, FailureKind::output_invalid);

  call.input = make_input(VALID, 14, -1);
  CHECK(entry(&call, &result) == 0);
  check_card_failure(result, 14);

  for (const auto scenario :
       {Extra::keyword, Extra::temperature, Extra::reading}) {
    extra = scenario;
    call.input = make_input(VALID, 0, -1);
    CHECK(entry(&call, &result) == 0);
    CheckSuccess(result, false, 0);
  }
  for (const auto scenario : {Extra::bad_box, Extra::empty_merge}) {
    extra = scenario;
    call.input = make_input(VALID, 0, -1);
    CHECK(entry(&call, &result) == 0);
    CHECK(Failure(result, FailureKind::invalid_request)
              .message()
              ->str()
              .find("arrival") != std::string::npos);
  }
  extra = Extra::none;
  CHECK(dlclose(library) == 0);
  return 0;
}
