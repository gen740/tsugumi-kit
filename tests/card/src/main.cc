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
  const float xyz[] = {1.0f};
  const uint32_t rgb[] = {0x123456};
  const double xy[] = {2.0};
  const card::Series series[] = {{"id", "line", xy, xy}};
  const card::Blob blobs[] = {{"sample", bytes}};

  return card::display("résumé\n\"") == 0 &&
         card::display(card::Markdown{"**text**"}, true) == 0 &&
         card::display(2.5) == 0 &&
         card::display(card::Table{columns, rows}) == 0 &&
         card::display(card::Image{"image/png", bytes, "sample"}) == 0 &&
         card::display(card::Points{xyz, xyz, xyz, rgb}) == 0 &&
         card::display(card::LineChart{series}) == 0 &&
         card::display(card::Plotly{R"({"data":[]})"}) == 0 &&
         card::display(card::VegaLite{R"({"mark":"point"})"}) == 0 &&
         card::display(card::Module{"custom", R"({"key":1})", blobs}, true) ==
             0;
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
    case 7: {
      Card::Output malformed;
      malformed.number.emplace();
      return malformed;
    }
  }

  Card::Output out;
  out.number.emplace().number = std::make_unique<outputs::number::ValueT>();
  out.number->number->value =
      input.number.position->x() + input.number.position->y();
  if (input.file) out.file.emplace().value = input.file->value;
  if (input.directory) out.directory.emplace().value = input.directory->value;
  return out;
}
