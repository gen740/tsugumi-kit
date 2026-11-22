#include "wire.h"

#include <dlfcn.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

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

auto AddPath(CardInputT& input, const char* name, std::string path) -> void {
  Add(input, name, PortKind::Path, {});
  input.inputs.back()->path = std::move(path);
}

enum class Arrival { temperature, reading, bad_bytes, empty };

auto AddArrival(CardInputT& input, Arrival arrival) -> void {
  using namespace inputs::arrival::tsugumi::merge;
  MergedT merged;
  if (arrival == Arrival::temperature || arrival == Arrival::bad_bytes) {
    t0::TemperatureT temperature;
    temperature.value = 20;
    TemperatureBoxT box;
    box.bytes = arrival == Arrival::bad_bytes
                    ? std::vector<uint8_t>{1, 2, 3}
                    : Pack<t0::Temperature>(temperature);
    merged.value.Set(std::move(box));
  } else if (arrival == Arrival::reading) {
    t1::ReadingT reading;
    reading.name = "sensor";
    ReadingBoxT box;
    box.bytes = Pack<t1::Reading>(reading);
    merged.value.Set(std::move(box));
  }
  Add(input, "arrival", PortKind::FlatBuffer, Pack<Merged>(merged));
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

auto InputDirectory() -> std::string {
  return std::filesystem::absolute("input-directory").string();
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
    if (scenario == WRONG_KIND) {
      AddPath(input, "number", InputFile());
    } else {
      Add(input, "number", kind,
          scenario == MALFORMED_PORT ? std::vector<uint8_t>{1, 2, 3}
                                     : number_bytes);
    }
    if (scenario == DUPLICATE_PORT) Add(input, "number", kind, number_bytes);
    if (scenario == UNKNOWN_PORT) Add(input, "unknown", kind, number_bytes);
  }
  Add(input, "table", PortKind::FlatBuffer,
      Pack<inputs::table::Table>(TableInput()));
  using Reserved = inputs::tsugumi_class_2::tsugumi_template::tsugumi_union;
  Reserved::NativeTableType reserved;
  reserved.return_ = 43;
  reserved.tsugumi_value = 7;
  Add(input, "Params", PortKind::FlatBuffer, Pack<Reserved>(reserved));
  if (scenario == OPTIONAL_PORTS) {
    AddPath(input, "file", InputFile());
    AddPath(input, "directory", InputDirectory());
    AddArrival(input, Arrival::temperature);
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
  CHECK(decoded.outputs()->size() == (has_file ? 7u : 3u));
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
          file.kind() == PortKind::Path);
    CHECK(forward.name()->string_view() == "forward" &&
          forward.kind() == PortKind::Path);
    CHECK(!file.bytes() && !forward.bytes());
    const auto output = file.path()->str();
    CHECK(output == "output.txt");
    CHECK(std::filesystem::is_regular_file(
        std::filesystem::symlink_status(output)));
    CHECK(Read(output) == "CARD INPUT\n");
    CHECK(forward.path()->str() == InputFile());
    const auto& directory = *decoded.outputs()->Get(4);
    CHECK(directory.name()->string_view() == "directory" &&
          directory.kind() == PortKind::Path);
    CHECK(directory.path()->str() == InputDirectory());
    CHECK(Read(std::filesystem::path(InputDirectory()) / "keep.txt") ==
          "directory input\n");
  }
  const auto& reserved = *decoded.outputs()->Get(has_file ? 5 : 2);
  CHECK(reserved.name()->string_view() == "switch" &&
        reserved.kind() == PortKind::FlatBuffer);
  const auto reserved_value =
      Unpack<outputs::tsugumi_concept::tsugumi_constexpr>(reserved);
  CHECK(reserved_value.return_ == 43 && reserved_value.tsugumi_value == 7);
  if (has_file) {
    const auto& arrival = *decoded.outputs()->Get(6);
    CHECK(arrival.name()->string_view() == "arrival" &&
          arrival.kind() == PortKind::FlatBuffer);
    using namespace outputs::arrival::tsugumi::merge;
    const auto merged = Unpack<Merged>(arrival);
    const auto* box = merged.value.AsTemperatureBox();
    CHECK(box);
    const Buffer bytes({box->bytes.data(), box->bytes.size()});
    CHECK(bytes.Verify<t0::Temperature>()->value() == 21);
  }
  CHECK(Read("initialization.marker") == "loaded\n");
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
  if (mode == 3) {
    const auto& failure = Failure(result, FailureKind::cancelled);
    CHECK(failure.message()->string_view() == "explicit failure");
    CHECK(failure.cell()->string_view() == "fit");
    CHECK(failure.source()->string_view() == "src/main.jl" &&
          failure.line() == 12);
  } else if (mode <= 5) {
    const auto& failure = Failure(result, FailureKind::worker_failed);
    if (mode == 4) {
      CHECK(failure.message()->string_view() == "failure from included source");
      CHECK(failure.source()->string_view() == "src/failure.jl");
      CHECK(failure.line() == 2);
    } else {
      CHECK(failure.message()->string_view() == "Card threw");
    }
  } else if (mode == 8) {
    const auto& failure = Failure(result, FailureKind::input_missing);
    CHECK(failure.message()->string_view() == "returned failure");
    CHECK(failure.cell()->string_view() == "load");
    CHECK(!failure.source() && failure.line() == 0);
  } else if (mode == 9) {
    const auto& failure = Failure(result, FailureKind::worker_failed);
    CHECK(failure.message()->string_view() == "reserved failure kind");
    CHECK(failure.cell()->string_view() == "fit");
    CHECK(failure.source()->string_view() == "src/main.jl" &&
          failure.line() == 18);
  } else if (mode == 18) {
    const auto& failure = Failure(result, FailureKind::input_missing);
    CHECK(failure.message()->string_view() == "Missing input unwired");
    CHECK(failure.source()->string_view() == "src/main.jl" &&
          failure.line() == 11);
  } else {
    Failure(result, FailureKind::output_invalid);
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
             {0, 0, 128, 63, 0, 0, 128, 63, 0, 0, 128, 63, 86, 52, 18, 0}},
      Sample{"line-chart",
             R"({"series":[{"count":1,"id":"id","label":"line","offset":0}]})",
             {0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 64}},
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

namespace {

auto Call(tsugumi_main_fn entry, const tsugumi_call& call) -> tsugumi_bytes {
  tsugumi_bytes result{};
  CHECK(entry(&call, &result) == 0);
  return result;
}

auto CorruptRootOffset(tsugumi_bytes bytes) -> void {
  CHECK(bytes.size >= FLATBUFFERS_MIN_BUFFER_SIZE);
  const auto offset = flatbuffers::EndianScalar(
      static_cast<flatbuffers::uoffset_t>(bytes.size));
  std::memcpy(const_cast<uint8_t*>(bytes.data), &offset, sizeof(offset));
}

auto CheckCorruptOffsets(tsugumi_main_fn entry) -> void {
  const auto envelope = make_input(VALID, 0, -1);
  CorruptRootOffset(envelope);
  Failure(Call(entry, {TSUGUMI_ABI_VERSION, envelope, nullptr, nullptr}),
          FailureKind::invalid_request);

  for (const auto params : {true, false}) {
    const auto bytes = make_input(VALID, 0, -1);
    const auto& input = Wire<CardInput>(bytes);
    const auto* nested =
        params ? input.params() : input.inputs()->Get(0)->bytes();
    CorruptRootOffset({nested->data(), nested->size()});
    Failure(Call(entry, {TSUGUMI_ABI_VERSION, bytes, nullptr, nullptr}),
            FailureKind::invalid_request);
  }
}

class SocketPair {
 public:
  SocketPair() { CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_.data()) == 0); }
  ~SocketPair() {
    CHECK(close(host()) == 0);
    CHECK(close(card()) == 0);
  }
  SocketPair(const SocketPair&) = delete;
  auto operator=(const SocketPair&) -> SocketPair& = delete;
  auto host() const -> int { return fds_[0]; }
  auto card() const -> int { return fds_[1]; }

 private:
  std::array<int, 2> fds_{};
};

auto Signal(int fd) -> void { CHECK(write(fd, "!", 1) == 1); }

auto Wait(int fd) -> void {
  char signal = 0;
  CHECK(read(fd, &signal, 1) == 1 && signal == '!');
}

std::promise<void>* returning = nullptr;

auto ExchangeWithCard(int fd) -> void {
  Signal(fd);
  Wait(fd);
}

auto GcDisplay(void* host, const char*, const uint8_t*, size_t) -> int {
  ExchangeWithCard(static_cast<SocketPair*>(host)->host());
  return 0;
}

auto GcCancelled(void* host) -> int {
  ExchangeWithCard(static_cast<SocketPair*>(host)->host());
  return 0;
}

auto GcRemainingMs(void* host) -> int64_t {
  ExchangeWithCard(static_cast<SocketPair*>(host)->host());
  return -1;
}

auto RaceDisplay(void*, const char*, const uint8_t*, size_t) -> int {
  returning->set_value();
  return 0;
}

auto RaceCancelled(void* host) -> int {
  ExchangeWithCard(static_cast<SocketPair*>(host)->host());
  return 1;
}

auto CheckRacingQuery(tsugumi_main_fn entry, int mode) -> void {
  using namespace std::chrono_literals;
  SocketPair sockets;
  std::promise<void> about_to_return;
  returning = &about_to_return;
  auto ready = about_to_return.get_future();
  const tsugumi_effects effects{RaceDisplay, RaceCancelled, GcRemainingMs};
  const tsugumi_call call{TSUGUMI_ABI_VERSION,
                          make_input(VALID, mode, sockets.card()), &sockets,
                          &effects};
  auto result =
      std::async(std::launch::async, [&] { return Call(entry, call); });
  ready.get();
  // The Card has reached its return path, but the host query still owns its
  // call context. Invalidation must wait for that query to finish.
  CHECK(result.wait_for(150ms) == std::future_status::timeout);
  Signal(sockets.card());
  check_success(result.get(), 0);
  check_success(Call(entry, {TSUGUMI_ABI_VERSION, make_input(VALID, 17, -1),
                             nullptr, nullptr}),
                0);
}

auto CheckMerge(tsugumi_main_fn entry) -> void {
  for (const auto arrival :
       {Arrival::reading, Arrival::bad_bytes, Arrival::empty}) {
    const auto valid = make_input(VALID, 0, -1);
    CardInputT input;
    Wire<CardInput>(valid).UnPackTo(&input);
    AddArrival(input, arrival);
    flatbuffers::FlatBufferBuilder builder;
    builder.Finish(CardInput::Pack(builder, &input));
    const auto result = Call(entry, {TSUGUMI_ABI_VERSION,
                                     {builder.GetBufferPointer(),
                                      builder.GetSize()},
                                     nullptr,
                                     nullptr});
    if (arrival != Arrival::reading) {
      CHECK(Failure(result, FailureKind::invalid_request)
                .message()
                ->str()
                .find("arrival") != std::string::npos);
      continue;
    }
    const auto& decoded = Wire<CardResult>(result);
    CHECK(!decoded.failure() && decoded.outputs()->size() == 4);
    const auto& port = *decoded.outputs()->Get(3);
    CHECK(port.name()->string_view() == "arrival");
    using namespace outputs::arrival::tsugumi::merge;
    const auto merged = Unpack<Merged>(port);
    const auto* box = merged.value.AsReadingBox();
    CHECK(box);
    const Buffer bytes({box->bytes.data(), box->bytes.size()});
    CHECK(bytes.Verify<t1::Reading>()->name()->str() == "sensor");
  }
}

}  // namespace

extern "C" auto shared_main(int argc, char** argv) -> int;

auto main(int argc, char** argv) -> int {
  CHECK(shared_main(argc, argv) == 0);
  void* card = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  CHECK(card);
  const auto entry =
      reinterpret_cast<tsugumi_main_fn>(dlsym(card, "tsugumi_main"));
  CHECK(entry);
  CheckCorruptOffsets(entry);

  for (const auto mode : {8, 9, 10, 18}) {
    const tsugumi_call call{TSUGUMI_ABI_VERSION, make_input(VALID, mode, -1),
                            nullptr, nullptr};
    check_card_failure(Call(entry, call), mode);
  }

  CheckMerge(entry);

  const tsugumi_effects throwing{
      [](void*, const char*, const uint8_t*, size_t) -> int {
        throw std::runtime_error("display");
      },
      [](void*) -> int { throw std::runtime_error("cancelled"); },
      [](void*) -> int64_t { throw std::runtime_error("remaining_ms"); },
  };
  check_success(Call(entry, {TSUGUMI_ABI_VERSION, make_input(VALID, 11, -1),
                             nullptr, &throwing}),
                0);

  for (const auto mode : {12, 13, 14}) {
    SocketPair sockets;
    const tsugumi_effects effects{GcDisplay, GcCancelled, GcRemainingMs};
    check_success(Call(entry, {TSUGUMI_ABI_VERSION,
                               make_input(VALID, mode, sockets.card()),
                               &sockets, &effects}),
                  0);
  }

  // The shared suite initialized on the main host thread. These calls arrive
  // from other host threads, with a subsequent call back on the main thread.
  CheckRacingQuery(entry, 15);
  CheckRacingQuery(entry, 16);
  CHECK(dlclose(card) == 0);
  puts("Julia ABI 9 audit regressions passed");
  return 0;
}
