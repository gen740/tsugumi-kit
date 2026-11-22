#include "wire.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
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
std::string input_path;
std::string directory_path;

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
  CHECK(port.bytes() && port.bytes()->size());
  const size_t size = port.bytes()->size();
  struct alignas(FLATBUFFERS_MAX_ALIGNMENT) Block {
    uint8_t bytes[FLATBUFFERS_MAX_ALIGNMENT];
  };
  std::vector<Block> aligned((size + sizeof(Block) - 1) / sizeof(Block));
  std::ranges::copy(*port.bytes(), reinterpret_cast<uint8_t*>(aligned.data()));
  return *std::unique_ptr<typename Table::NativeTableType>(
      Decode<Table>({reinterpret_cast<const uint8_t*>(aligned.data()), size})
          .UnPack());
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

auto AddPath(CardInputT& input, const char* name, const std::string& path)
    -> void {
  auto port = std::make_unique<CardPortT>();
  port->name = name;
  port->kind = PortKind::Path;
  port->path = path;
  input.inputs.push_back(std::move(port));
}

auto ReadPath(const CardResult& output, size_t index, std::string_view name)
    -> std::string {
  const auto* port = output.outputs()->Get(index);
  CHECK(port->name()->string_view() == name && port->kind() == PortKind::Path);
  CHECK(!port->bytes() || port->bytes()->size() == 0);
  CHECK(port->path());
  return port->path()->str();
}

auto CheckFailure(tsugumi_bytes result, FailureKind kind)
    -> const CardFailure& {
  const auto& decoded = Decode<CardResult>(result);
  CHECK(decoded.failure() && decoded.failure()->kind() == kind);
  CHECK(decoded.failure()->message() && decoded.failure()->message()->size());
  CHECK(!decoded.outputs() || !decoded.outputs()->size());
  return *decoded.failure();
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

  inputs::data::DataT data;
  data.rows.push_back(std::make_unique<inputs::data::RowT>());
  data.rows.front()->value = 2;
  inputs::data::TextT text;
  text.text = "value";
  data.choice.Set(std::move(text));
  AddPort(input, "data", PortKind::FlatBuffer,
          Encode<inputs::data::Data>(data));

  if (test_case != MISSING_INPUT) {
    inputs::number::NumberT number;
    number.position = std::make_unique<inputs::number::Position>(40, -15);
    const auto kind = test_case == UNKNOWN_KIND ? static_cast<PortKind>(255)
                      : test_case == WRONG_KIND ? PortKind::Path
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
    input_path = std::filesystem::absolute("input.txt").string();
    std::ofstream file(input_path);
    file << "read from Path input";
    CHECK(file.good());
    AddPath(input, "file", input_path);

    directory_path = std::filesystem::absolute("input-directory").string();
    std::filesystem::create_directory(directory_path);
    std::ofstream member(std::filesystem::path(directory_path) / "member.txt");
    member << "forwarded directory member";
    CHECK(member.good());
    AddPath(input, "directory", directory_path);
  }

  if (mode == 21 || mode == 22 || mode == 23 || mode == 26) {
    using namespace inputs::arrival::tsugumi::merge;
    MergedT merged;
    if (mode == 26) {
      t1::ReadingT value;
      value.name = "reading";
      ReadingBoxT box;
      box.bytes = Encode<t1::Reading>(value);
      merged.value.Set(std::move(box));
    } else if (mode != 23) {
      t0::nested::t0::TemperatureT value;
      value.value = 41.5;
      t0::nested::TemperatureBoxT temperature;
      temperature.bytes = mode == 22
                              ? std::vector<uint8_t>{1, 2, 3}
                              : Encode<t0::nested::t0::Temperature>(value);
      t0::nested::MergedT nested;
      nested.value.Set(std::move(temperature));
      MergedBoxT box;
      box.bytes = Encode<t0::nested::Merged>(nested);
      merged.value.Set(std::move(box));
    }
    AddPort(input, "arrival", PortKind::FlatBuffer, Encode<Merged>(merged));
  }
  if (mode == 25) {
    AddPath(input, "file", "relative.txt");
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
  const auto& ports = *decoded.outputs();
  CHECK(ports.size() == (optional_ports ? 5u : 2u));
  const auto number = DecodePort<outputs::number::Number>(
      *ports.Get(0), "number", PortKind::FlatBuffer);
  CHECK(number.position && number.position->x() == 42 &&
        number.position->y() == -15);
  const auto data = DecodePort<outputs::data::Data>(*ports.Get(1), "data",
                                                    PortKind::FlatBuffer);
  CHECK(data.rows.size() == 1 && data.rows.front()->value == 2);
  CHECK(data.choice.AsCount() && data.choice.AsCount()->value == 5);
  if (optional_ports) {
    const auto file = ReadPath(decoded, 2, "file");
    CHECK(file == "result.txt");
    CHECK(std::filesystem::is_regular_file(
        std::filesystem::symlink_status(file)));
    std::ifstream content(file);
    const std::string text(std::istreambuf_iterator<char>{content}, {});
    CHECK(text == "read from Path input");
    CHECK(ReadPath(decoded, 3, "forwarded") == input_path);
    const auto directory = ReadPath(decoded, 4, "directory");
    CHECK(directory == directory_path);
    CHECK(std::filesystem::is_regular_file(std::filesystem::path(directory) /
                                           "member.txt"));
  }
}

extern "C" auto check_input_failure(tsugumi_bytes result, input_case test_case)
    -> void {
  const auto& failure = CheckFailure(
      result, test_case == MISSING_INPUT ? FailureKind::input_missing
                                         : FailureKind::invalid_request);
  if (test_case == MISSING_INPUT)
    CHECK(failure.message()->string_view() == "Missing input number");
}

extern "C" auto check_card_failure(tsugumi_bytes result, int mode) -> void {
  switch (mode) {
    case 3: {
      const auto& failure = CheckFailure(result, FailureKind::cancelled);
      CHECK(failure.message()->string_view() == "explicit failure");
      CHECK(failure.source() &&
            failure.source()->string_view() == "src/card.go");
      CHECK(failure.line() > 0);
      CHECK(failure.cell() && failure.cell()->string_view() == "fit");
      break;
    }
    case 4:
      CHECK(CheckFailure(result, FailureKind::worker_failed)
                .message()
                ->string_view() == "ordinary error");
      break;
    case 5:
      CHECK(CheckFailure(result, FailureKind::worker_failed)
                .message()
                ->string_view() == "Go panic: test panic");
      break;
    case 8:
      CHECK(CheckFailure(result, FailureKind::cancelled)
                .message()
                ->string_view() == "host cancelled");
      break;
    case 9:
      CHECK(CheckFailure(result, FailureKind::worker_failed)
                .message()
                ->string_view() == "Go panic: background panic");
      break;
    default:
      CheckFailure(result, FailureKind::output_invalid);
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
      Sample{"points", R"({"count":2})", {0,  0,  128, 63, 0,   0,   0,   192,
                                          0,  0,  64,  64, 0,   0,   128, 64,
                                          0,  0,  160, 64, 0,   0,   192, 64,
                                          86, 52, 18,  0,  221, 204, 187, 170}},
      Sample{
          "line-chart",
          R"({"series":[{"count":2,"id":"a","label":"first","offset":0},{"count":1,"id":"b","label":"second","offset":2}]})",
          {0, 0, 0, 0, 0, 0, 0,  64, 0, 0, 0, 0, 0, 0, 16, 192,
           0, 0, 0, 0, 0, 0, 24, 64, 0, 0, 0, 0, 0, 0, 8,  64,
           0, 0, 0, 0, 0, 0, 20, 64, 0, 0, 0, 0, 0, 0, 28, 192}},
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

extern "C" auto check_revision2(tsugumi_main_fn entry) -> void {
  using namespace outputs::arrival::tsugumi::merge;
  for (const int mode : {20, 21, 22, 23, 24, 25, 26}) {
    const tsugumi_call call{TSUGUMI_ABI_VERSION, make_input(VALID, mode, -1),
                            nullptr, nullptr};
    tsugumi_bytes result{};
    CHECK(entry(&call, &result) == 0);
    if (mode == 20) {
      CHECK(CheckFailure(result, FailureKind::input_missing)
                .message()
                ->string_view() == "Missing input unwired");
    } else if (mode == 22 || mode == 23 || mode == 25) {
      CheckFailure(result, FailureKind::invalid_request);
    } else if (mode == 24) {
      CheckFailure(result, FailureKind::output_invalid);
    } else {
      const auto& decoded = Decode<CardResult>(result);
      CHECK(!decoded.failure() && decoded.outputs()->size() == 3);
      const auto merged = DecodePort<Merged>(*decoded.outputs()->Get(2),
                                             "arrival", PortKind::FlatBuffer);
      if (mode == 21) {
        const auto* box = merged.value.AsMergedBox();
        CHECK(box);
        t0::nested::MergedT nested;
        Decode<t0::nested::Merged>({box->bytes.data(), box->bytes.size()})
            .UnPackTo(&nested);
        const auto* member = nested.value.AsTemperatureBox();
        CHECK(member);
        CHECK(Decode<t0::nested::t0::Temperature>(
                  {member->bytes.data(), member->bytes.size()})
                  .value() == 42.5);
      } else {
        const auto* box = merged.value.AsReadingBox();
        CHECK(box);
        CHECK(Decode<t1::Reading>({box->bytes.data(), box->bytes.size()})
                  .name()
                  ->string_view() == "reading");
      }
    }
  }
}
