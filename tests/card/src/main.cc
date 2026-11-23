#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "card.hh"

auto run(const Card::Inputs& input) -> Card::Output {
  const auto& arrival = input.arrival.value();
  const auto& granted = std::visit(
      [](const auto& item) -> const card::Artifact& { return *item.file; },
      arrival);
  const auto opened = granted.open();
  const auto& batch = input.batch.value();
  if (batch.items.size() != 1 ||
      batch.items.front()->schema != std::string(64, '4') ||
      input.direct->open() != opened) {
    throw std::runtime_error("Unexpected Collect or Artifact input");
  }
  const auto* element = flatbuffers::GetRoot<inputs::batch::Element>(
      batch.items.front()->bytes.data());
  if (element->value() != 4)
    throw std::runtime_error("Unexpected Collect item");
  if (!std::filesystem::is_directory(opened) || !Card::params.flag ||
      Card::params.ratio != 1.0 || Card::params.labels.at(0) != "日本語" ||
      card::cancelled() || card::remaining_ms() != 500) {
    throw std::runtime_error("Unexpected contract fixture values");
  }
  const auto* number = std::get_if<inputs::arrival::NumberT>(&arrival);
  if (number && number->value < 0) return {};

  auto [artifact, directory] = card::Artifact::dir();
  const auto file = directory / "value.txt";
  std::ofstream(file) << "test";
  auto upload = card::Artifact::upload(file);

  using card::display::ReceiveStatus;
  const auto message = card::display::recv("request", -1);
  if (message.status != ReceiveStatus::kMessage ||
      message.bytes != std::vector<uint8_t>{42} ||
      card::display::recv("request", 0).status != ReceiveStatus::kTimeout ||
      card::display::recv("request", 1).status != ReceiveStatus::kCancelled) {
    throw std::runtime_error("Unexpected receive status");
  }

  auto send = [](size_t size) {
    if (card::display::send("listing", std::vector<uint8_t>(size, 42)) != 0)
      throw std::runtime_error("Unexpected send status");
  };
  send(1);
  send((1u << 24) + 5);
  std::thread background([] {
    const auto reply = card::display::recv("background", -1);
    if (reply.status == ReceiveStatus::kMessage)
      card::display::send("background", reply.bytes);
  });
  background.detach();

  Card::Output output;
  auto& listing = output.listing.emplace();
  listing.value = Card::params.offset + (number ? number->value : 20);
  listing.nested = std::make_unique<outputs::ChildT>();
  listing.nested->file = std::make_unique<card::Artifact>(std::move(artifact));
  listing.files.push_back(std::make_unique<card::Artifact>(std::move(upload)));
  listing.files.push_back(std::make_unique<card::Artifact>(granted));
  auto [choice, unused] = card::Artifact::dir();
  listing.choice.Set(std::move(choice));
  return output;
}
