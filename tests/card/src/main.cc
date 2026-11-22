#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "card.hh"

#ifndef ABI_TEST_CARD
#error The builder must pass cxxflags through to the Card.
#endif

namespace {

std::thread background;
const bool path_configured_at_load = std::getenv("PATH") != nullptr;

auto DisplaySamples() -> bool {
  const std::vector<std::string> columns{"n"};
  const std::vector<std::vector<std::string>> rows{{"1"}};
  const uint8_t bytes[] = {0, 13, 255};
  const float point[] = {1};
  const uint32_t colors[] = {0x123456};
  const double x[] = {2};
  const double y[] = {3};
  const card::display::Series series[] = {{"id", "line", x, y}};

  return card::display::send("résumé\n\"") == 0 &&
         card::display::send(card::display::Markdown{"**text**"}) == 0 &&
         card::display::send(2.5) == 0 &&
         card::display::send(card::display::Table{columns, rows}) == 0 &&
         card::display::send(
             card::display::Image{"image/png", bytes, "sample"}) == 0 &&
         card::display::send(
             card::display::Points{point, point, point, colors}) == 0 &&
         card::display::send(card::display::LineChart{series}) == 0 &&
         card::display::send("custom", bytes) == 0;
}

auto CheckTools(bool declared) -> void {
  if (path_configured_at_load != declared)
    throw std::runtime_error("Unexpected PATH during Card initialization");
  char tool[] = "tsugumi-test-tool";
  char* const args[] = {tool, nullptr};
  char* const environment[] = {nullptr};
  pid_t pid;
  const int error =
      posix_spawnp(&pid, tool, nullptr, nullptr, args, environment);
  if (error == ENOENT && !declared) return;
  if (error != 0) throw std::runtime_error("Could not start the declared tool");
  int status;
  if (waitpid(pid, &status, 0) != pid || !declared || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 42)
    throw std::runtime_error("Unexpected tool result");
}

}  // namespace

auto run(const Card::Inputs& input) -> Card::Output {
  const auto number_input = input.get<Card::input_types::number>("number");
  using Path = std::filesystem::path;
  if (input.get<std::optional<Path>>("unwired"))
    throw std::runtime_error("Unwired input should be absent");
  if (input.params.mode == 20) (void)input.get<Path>("unwired");
  // Exercise both the dependency's header search path and its link flag.
  if (zlibVersion()[0] != ZLIB_VERSION[0]) {
    throw std::runtime_error("Unexpected zlib version");
  }
  using Tsugumi::Data::FailureKind;
  switch (input.params.mode) {
    case 1: {
      bool active = false;
      std::thread helper(
          [&] { active = !card::cancelled() && card::remaining_ms() == 500; });
      helper.join();
      if (!active || !DisplaySamples())
        throw std::runtime_error("Active effects failed");
      background = std::thread([fd = input.params.fd] {
        char signal;
        if (read(fd, &signal, 1) != 1 || !card::cancelled() ||
            card::remaining_ms() != 0 || card::display::send("outside") != 0)
          std::terminate();
      });
      break;
    }
    case 2:
      background.join();
      break;
    case 3:
      throw tsugumi::Failure(FailureKind::cancelled, "explicit failure",
                             std::source_location::current(), "fit");
    case 4:
      throw std::runtime_error("standard exception");
    case 5:
      throw 5;
    case 6:
      return {};
    case 13:
      throw tsugumi::Failure(FailureKind::unspecified, "reserved failure");
    case 14:
      throw tsugumi::Failure(static_cast<FailureKind>(65535),
                             "unknown failure");
    case 15:
      if (!card::cancelled() || card::remaining_ms() != -1)
        throw std::runtime_error("Cancelled call without a deadline");
      break;
    case 16:
    case 17: {
      const int fd = input.params.fd;
      const bool poll_cancelled = input.params.mode == 16;
      background = std::thread([poll_cancelled] {
        if (poll_cancelled ? !card::cancelled() : card::remaining_ms() != -1)
          std::terminate();
      });
      char signal;
      if (read(fd, &signal, 1) != 1 || write(fd, "!", 1) != 1)
        throw std::runtime_error("Could not synchronize the host query");
      break;
    }
    case 18:
    case 19:
      CheckTools(input.params.mode == 18);
      break;
  }

  Card::Output out;
  using outputs::number::Choice;
  using outputs::number::NestedT;
  using outputs::number::ValueT;
  auto& number = out.number.emplace();
  number.number = std::make_unique<ValueT>();
  number.number->value =
      number_input.position->x() + number_input.position->y();
  number.choices.emplace_back().Set(std::string("valid"));
  NestedT nested;
  nested.return_ = std::make_unique<ValueT>();
  number.required_choice.Set(std::move(nested));
  switch (input.params.mode) {
    case 7:
      number.number.reset();
      break;
    case 8:
      number.children.emplace_back();
      break;
    case 9:
      number.choice.type = Choice::Nested;
      break;
    case 10:
      number.required_choice.Reset();
      break;
    case 11:
      number.choice.Set(NestedT{});
      break;
    case 12:
      number.choices.emplace_back();
      break;
  }
  out.file = input.get<std::optional<Path>>("file");
  out.directory = input.get<std::optional<Path>>("directory");
  if (input.arrival) {
    using namespace outputs::arrival::tsugumi::merge;
    if (const auto* temperature =
            std::get_if<inputs::arrival::tsugumi::merge::t0::TemperatureT>(
                &*input.arrival)) {
      t0::TemperatureT value;
      value.value = temperature->value + 1;
      out.arrival = std::move(value);
    } else {
      const auto& reading =
          std::get<inputs::arrival::tsugumi::merge::t1::ReadingT>(
              *input.arrival);
      t1::ReadingT value;
      value.name = reading.name;
      out.arrival = std::move(value);
    }
  }
  if (input.Params) out.switch_.emplace().value = input.Params->value + 1;
  return out;
}
