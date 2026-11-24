#include "card.hh"

#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

auto run(const Card::Inputs&) -> Card::Output {
  // The wire format is little-endian f32: three values out and two back.
  static_assert(std::endian::native == std::endian::little &&
                sizeof(float) == 4);
  const auto rgb = std::bit_cast<std::array<uint8_t, 12>>(
      std::array{static_cast<float>(Card::params.red),
                 static_cast<float>(Card::params.green),
                 static_cast<float>(Card::params.blue)});
  using namespace card::display;
  const auto view = view_open("clear");
  if (send_raw(view.session, rgb) != 0) {
    throw std::runtime_error("Could not send the colour");
  }

  // Join before returning so the click is logged with this invocation.
  std::jthread click([] {
    try {
      const auto reply = recv(8000);
      std::array<float, 2> position;
      if (reply.status == ReceiveStatus::kMessage &&
          reply.bytes.size() == sizeof(position)) {
        std::memcpy(position.data(), reply.bytes.data(), sizeof(position));
        std::printf("Click at (%.1f, %.1f)\n", position[0], position[1]);
      }
    } catch (const std::exception& error) {
      std::fprintf(stderr, "%s\n", error.what());
    }
  });
  return {};
}
