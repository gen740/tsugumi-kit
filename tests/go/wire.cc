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

auto CheckFailure(tsugumi_bytes result, FailureKind kind)
    -> const CardFailure& {
  const auto& decoded = Decode<CardResult>(result);
  CHECK(decoded.failure() && decoded.failure()->kind() == kind);
  CHECK(decoded.failure()->message() && decoded.failure()->message()->size());
  CHECK(!decoded.outputs() || !decoded.outputs()->size());
  return *decoded.failure();
}

auto CheckBlob(const CardDisplay& display, size_t index, std::string_view name,
               std::initializer_list<uint8_t> expected) -> void {
  const auto& blob = *display.blobs()->Get(index);
  CHECK(blob.name()->string_view() == name);
  CHECK(blob.bytes() && std::ranges::equal(*blob.bytes(), expected));
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
                      : test_case == WRONG_KIND ? PortKind::File
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
    file << "read from File input";
    CHECK(file.good());
    inputs::file::FileT port;
    port.value = input_path;
    AddPort(input, "file", PortKind::File, Encode<inputs::file::File>(port));

    directory_path = std::filesystem::absolute("input-directory").string();
    CHECK(std::filesystem::create_directory(directory_path));
    std::ofstream member(std::filesystem::path(directory_path) / "member.txt");
    member << "forwarded Directory member";
    CHECK(member.good());
    inputs::directory::DirectoryT directory;
    directory.value = directory_path;
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
    const auto file =
        DecodePort<outputs::file::File>(*ports.Get(2), "file", PortKind::File);
    CHECK(file.value == "result.txt");
    CHECK(std::filesystem::is_regular_file(
        std::filesystem::symlink_status(file.value)));
    std::ifstream content(file.value);
    const std::string text(std::istreambuf_iterator<char>{content}, {});
    CHECK(text == "read from File input");
    const auto forwarded = DecodePort<outputs::forwarded::File>(
        *ports.Get(3), "forwarded", PortKind::File);
    CHECK(forwarded.value == input_path);
    const auto directory = DecodePort<outputs::directory::Directory>(
        *ports.Get(4), "directory", PortKind::Directory);
    CHECK(directory.value == directory_path);
    CHECK(std::filesystem::is_regular_file(
        std::filesystem::path(directory.value) / "member.txt"));
  }
}

extern "C" auto check_input_failure(tsugumi_bytes result, input_case test_case)
    -> void {
  CheckFailure(result, test_case == MISSING_INPUT
                           ? FailureKind::input_missing
                           : FailureKind::invalid_request);
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

extern "C" auto check_display(tsugumi_bytes bytes, int record, unsigned index)
    -> void {
  struct Sample {
    std::string_view module, config;
    size_t blobs;
    int record = 0;
  };
  constexpr std::array samples{
      Sample{"tsugumi:text", R"({"text":"résumé\n\""})", 0},
      Sample{"tsugumi:markdown", R"({"text":"**text**"})", 0, 1},
      Sample{"tsugumi:markdown", R"({"text":"2.5"})", 0},
      Sample{"tsugumi:table", R"({"columns":["n"],"rows":[["1"]]})", 0},
      Sample{"tsugumi:image", R"({"alt":"sample","media_type":"image/png"})",
             1},
      Sample{"tsugumi:points", R"({"count":2})", 4},
      Sample{
          "tsugumi:line-chart",
          R"({"series":[{"count":2,"id":"a","label":"first","offset":0},{"count":1,"id":"b","label":"second","offset":2}]})",
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
    CheckBlob(display, 0, "x", {0, 0, 128, 63, 0, 0, 0, 192});
    CheckBlob(display, 1, "y", {0, 0, 64, 64, 0, 0, 128, 64});
    CheckBlob(display, 2, "z", {0, 0, 160, 64, 0, 0, 192, 64});
    CheckBlob(display, 3, "colors", {86, 52, 18, 0, 221, 204, 187, 170});
  }
  if (index == 6) {
    CheckBlob(display, 0, "x", {0, 0, 0,  0,   0, 0, 0, 64, 0, 0, 0,  0,
                                0, 0, 16, 192, 0, 0, 0, 0,  0, 0, 24, 64});
    CheckBlob(display, 1, "y", {0, 0, 0,  0,  0, 0, 8, 64, 0, 0, 0,  0,
                                0, 0, 20, 64, 0, 0, 0, 0,  0, 0, 28, 192});
  }
  if (index == 9) CheckBlob(display, 0, "sample", {0, 13, 255});
}
