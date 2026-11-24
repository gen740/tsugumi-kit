#include "wire.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <string_view>
#include <vector>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "tsugumi/protocol/card_generated.h"

namespace {

using Tsugumi::Card::CardInput;
using Tsugumi::Card::CardPort;
using Tsugumi::Card::CardResult;
using Tsugumi::Data::FailureKind;

flatbuffers::DetachedBuffer storage;

template <class Builder>
auto Build(Builder build) -> flatbuffers::DetachedBuffer {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(build(builder));
  return builder.Release();
}

// ids[0] of the fake host, or an id it does not know.
auto InputArtifact(bool known) -> tsugumi::ArtifactId {
  std::array<uint8_t, 16> bytes{};
  if (known) {
    bytes[0] = 1;
  } else {
    bytes.fill(0xee);
  }
  return tsugumi::ArtifactId(flatbuffers::span<const uint8_t, 16>(bytes));
}

}  // namespace

auto make_input(input_case scenario) -> tsugumi_bytes {
  if (scenario == MALFORMED_ENVELOPE) return {};
  const bool text = scenario == VALID_TEXT;
  const auto id = InputArtifact(scenario != UNKNOWN_ARTIFACT);
  auto payload = Build([&](auto& builder) -> flatbuffers::Offset<void> {
    const auto file = tsugumi::CreateArtifact(builder, &id);
    if (text)
      return inputs::arrival::CreateTextDirect(builder, "hello", file).Union();
    return inputs::arrival::CreateNumber(
               builder, scenario == MISSING_OUTPUT ? -1 : 7, file)
        .Union();
  });
  auto artifact = Build(
      [&](auto& builder) { return tsugumi::CreateArtifact(builder, &id); });
  auto element = Build(
      [](auto& builder) { return inputs::batch::CreateElement(builder, 4); });
  auto batch = Build([&](auto& builder) {
    const auto member = tsugumi::builtin::CreateEnvelope(
        builder, builder.CreateString(std::string(64, '4')),
        builder.CreateVector(element.data(), element.size()));
    const flatbuffers::Offset<tsugumi::builtin::Envelope> items[] = {member};
    return tsugumi::builtin::CreateCollected(builder,
                                             builder.CreateVector(items, 1));
  });
  storage = Build([&](auto& builder) {
    std::vector<flatbuffers::Offset<CardPort>> ports;
    if (scenario != MISSING_INPUT) {
      const std::string schema(64, scenario == UNKNOWN_SCHEMA ? '3'
                                   : text                     ? '2'
                                                              : '1');
      const auto bytes = builder.CreateVector(
          payload.data(),
          scenario == MALFORMED_PORT ? size_t{1} : payload.size());
      const auto port = Tsugumi::Card::CreateCardPort(
          builder,
          builder.CreateString(scenario == UNKNOWN_PORT ? "unknown"
                                                        : "arrival"),
          builder.CreateString(schema), bytes);
      ports.push_back(port);
      if (scenario == DUPLICATE_PORT) ports.push_back(port);
    }
    auto add_port = [&](const char* name, const char* schema,
                        const auto& value) {
      const auto port = Tsugumi::Card::CreateCardPort(
          builder, builder.CreateString(name), builder.CreateString(schema),
          builder.CreateVector(value.data(), value.size()));
      ports.push_back(port);
    };
    add_port("batch",
             "1c934d38885fb58ebdf2370bed0862db46f2bfa62677fe6bb801af8e4ce1d8e6",
             batch);
    add_port("direct",
             "7435657903802bed9dbd0ee94f0be71ff432073805511fc15af8acca6ac83bad",
             artifact);
    return Tsugumi::Card::CreateCardInput(builder, builder.CreateVector(ports));
  });
  return {storage.data(), storage.size()};
}

auto check_result(tsugumi_bytes result, input_case scenario,
                  const tsugumi_artifact_id* expected, size_t count) -> void {
  flatbuffers::Verifier verifier(result.data, result.size);
  CHECK(verifier.VerifyBuffer<CardResult>());
  const auto* wire = flatbuffers::GetRoot<CardResult>(result.data);
  if (scenario != VALID_NUMBER && scenario != VALID_TEXT) {
    CHECK(wire->failure() && !wire->outputs());
    const auto kind = wire->failure()->kind();
    if (scenario == UNKNOWN_ARTIFACT) {
      CHECK(kind != FailureKind::unspecified && wire->failure()->message());
      const auto message = wire->failure()->message()->string_view();
      if (message.find("artifact eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee is not "
                       "available") == std::string_view::npos) {
        fprintf(stderr, "Unexpected unknown Artifact message: %s\n",
                wire->failure()->message()->c_str());
        CHECK(false);
      }
    } else if (scenario == MISSING_OUTPUT) {
      CHECK(kind == FailureKind::output_invalid);
    } else if (scenario == MISSING_INPUT || scenario == UNKNOWN_PORT) {
      CHECK(kind == FailureKind::input_missing ||
            kind == FailureKind::input_invalid);
    } else {
      CHECK(kind == FailureKind::input_invalid);
    }
    return;
  }
  if (wire->failure()) {
    fprintf(stderr, "Unexpected Card failure: %s\n",
            wire->failure()->message()->c_str());
  }
  CHECK(!wire->failure() && wire->outputs() && wire->outputs()->size() == 1);
  const auto* port = wire->outputs()->Get(0);
  CHECK(port->name()->string_view() == "listing");
  const auto* bytes = port->bytes();
  alignas(FLATBUFFERS_MAX_ALIGNMENT) std::array<uint8_t, 4096> payload{};
  CHECK(bytes->size() <= payload.size());
  std::memcpy(payload.data(), bytes->data(), bytes->size());
  flatbuffers::Verifier value_verifier(payload.data(), bytes->size());
  CHECK(value_verifier.VerifyBuffer<outputs::Listing>());
  const auto* listing = flatbuffers::GetRoot<outputs::Listing>(payload.data());
  CHECK(listing->value() == (scenario == VALID_NUMBER ? 107 : 120));
  CHECK(listing->files()->size() == 2);
  const tsugumi::ArtifactId* nested[] = {
      listing->nested()->file()->id(),
      listing->files()->Get(0)->id(),
      listing->files()->Get(1)->id(),
      listing->choice_as_File()->id(),
  };
  // The listing holds the three created Artifacts and the forwarded input.
  CHECK(count == std::size(nested));
  const auto has_id = [&](const uint8_t* id) {
    return std::ranges::any_of(nested, [&](const auto* item) {
      return std::memcmp(id, item->bytes()->Data(), 16) == 0;
    });
  };
  for (size_t i = 0; i < count; ++i) CHECK(has_id(expected[i].bytes));
}
