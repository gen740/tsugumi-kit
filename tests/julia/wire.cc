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
    if (scenario == WRONG_KIND) kind = PortKind::File;
    Add(input, "number", kind,
        scenario == MALFORMED_PORT ? std::vector<uint8_t>{1, 2, 3}
                                   : number_bytes);
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
    inputs::file::FileT file;
    file.value = InputFile();
    Add(input, "file", PortKind::File, Pack<inputs::file::File>(file));
    inputs::directory::DirectoryT directory;
    directory.value = InputDirectory();
    Add(input, "directory", PortKind::Directory,
        Pack<inputs::directory::Directory>(directory));
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
  CHECK(decoded.outputs()->size() == (has_file ? 6u : 3u));
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
    const auto& directory = *decoded.outputs()->Get(4);
    CHECK(directory.name()->string_view() == "directory" &&
          directory.kind() == PortKind::Directory);
    CHECK(Unpack<outputs::directory::Directory>(directory).value ==
          InputDirectory());
    CHECK(Read(std::filesystem::path(InputDirectory()) / "keep.txt") ==
          "directory input\n");
  }
  const auto& reserved = *decoded.outputs()->Get(has_file ? 5 : 2);
  CHECK(reserved.name()->string_view() == "switch" &&
        reserved.kind() == PortKind::FlatBuffer);
  const auto reserved_value =
      Unpack<outputs::tsugumi_concept::tsugumi_constexpr>(reserved);
  CHECK(reserved_value.return_ == 43 && reserved_value.tsugumi_value == 7);
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
    CHECK(failure.source()->string_view() == "src/main.jl" &&
          failure.line() == 9);
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
          failure.line() == 15);
  } else {
    Failure(result, FailureKind::output_invalid);
  }
}

extern "C" auto check_display(tsugumi_bytes bytes, int record, unsigned index)
    -> void {
  constexpr std::array<std::string_view, 11> views{
      "tsugumi:text",       "tsugumi:markdown",
      "tsugumi:markdown",   "tsugumi:table",
      "tsugumi:image",      "tsugumi:points",
      "tsugumi:line-chart", "tsugumi:plotly",
      "tsugumi:vega-lite",  "custom",
      "tsugumi:text"};
  const auto& display = Wire<CardDisplay>(bytes);
  CHECK(index < views.size());
  CHECK(display.view()->string_view() == views[index]);
  CHECK(record == (index == 1 || index == 9));
  // Every display starts with its "config" blob.
  const auto blob = [&](unsigned i, std::string_view name) {
    CHECK(display.blobs() && i < display.blobs()->size());
    const auto* entry = display.blobs()->Get(i);
    CHECK(entry->name()->string_view() == name && entry->bytes());
    return std::string_view{
        reinterpret_cast<const char*>(entry->bytes()->data()),
        entry->bytes()->size()};
  };
  const auto config = blob(0, "config");
  if (index == 0) CHECK(config == R"({"text":"résumé\n\""})");
  if (index == 3) CHECK(config == R"({"columns":["n"],"rows":[["1"]]})");
  if (index == 9) {
    CHECK(config == R"({"key":1})");
    CHECK(display.blobs()->size() == 2);
    CHECK(blob(1, "sample") == std::string_view("\x00\x0d\xff", 3));
  }
  if (index == 10) CHECK(config == R"({"text":"outside"})");
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

// Display has process lifetime and does not take the call's host context.
int gc_display_fd = -1;
std::promise<void>* returning = nullptr;

auto ExchangeWithCard(int fd) -> void {
  Signal(fd);
  Wait(fd);
}

auto GcDisplay(tsugumi_bytes, int) -> int {
  ExchangeWithCard(gc_display_fd);
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

auto RaceDisplay(tsugumi_bytes, int) -> int {
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

  for (const auto mode : {8, 9, 10}) {
    const tsugumi_call call{TSUGUMI_ABI_VERSION, make_input(VALID, mode, -1),
                            nullptr, nullptr};
    check_card_failure(Call(entry, call), mode);
  }

  const tsugumi_effects throwing{
      [](tsugumi_bytes, int) -> int { throw std::runtime_error("display"); },
      [](void*) -> int { throw std::runtime_error("cancelled"); },
      [](void*) -> int64_t { throw std::runtime_error("remaining_ms"); },
  };
  check_success(Call(entry, {TSUGUMI_ABI_VERSION, make_input(VALID, 11, -1),
                             nullptr, &throwing}),
                0);

  for (const auto mode : {12, 13, 14}) {
    SocketPair sockets;
    gc_display_fd = sockets.host();
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
