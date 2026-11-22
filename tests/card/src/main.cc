#include <unistd.h>
#include <zlib.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "card.hh"

#ifndef ABI9_TEST_CARD
#error The builder must pass cxxflags through to the Card.
#endif

namespace {

std::thread background;

auto DisplaySamples() -> bool {
  const std::vector<std::string> columns{"n"};
  const std::vector<std::vector<std::string>> rows{{"1"}};
  const uint8_t bytes[] = {0, 13, 255};
  const card::Blob blobs[] = {{"config", card::text_bytes("mode=fast")},
                              {"sample", bytes}};

  return card::display("résumé\n\"") == 0 &&
         card::display(card::Markdown{"**text**"}, true) == 0 &&
         card::display(2.5) == 0 &&
         card::display(card::Table{columns, rows}) == 0 &&
         card::display(card::Image{"image/png", bytes, "sample"}) == 0 &&
         card::display(card::View{"custom", blobs}, true) == 0;
}

}  // namespace

auto run(const Card::Inputs& input) -> Card::Output {
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
            card::remaining_ms() != 0 || card::display("outside") != 0)
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
  }

  Card::Output out;
  using outputs::number::Choice;
  using outputs::number::NestedT;
  using outputs::number::ValueT;
  auto& number = out.number.emplace();
  number.number = std::make_unique<ValueT>();
  number.number->value =
      input.number.position->x() + input.number.position->y();
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
  if (input.file) out.file.emplace().value = input.file->value;
  if (input.directory) out.directory.emplace().value = input.directory->value;
  if (input.Params) out.switch_.emplace().value = input.Params->value + 1;
  return out;
}
