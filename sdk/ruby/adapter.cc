#include "adapter.hh"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>

#include "bridge.h"
#include "json_codec.hh"

extern "C" auto tsugumi_ruby_view_open(const char* view, uint32_t mode,
                                       uint32_t flags, uint32_t width,
                                       uint32_t height, uint8_t* route,
                                       uint8_t* session) -> int {
  try {
    const auto opened =
        card::display::view_open(view, mode, flags, width, height);
    std::memcpy(route, opened.route.bytes, sizeof(opened.route.bytes));
    std::memcpy(session, opened.session.bytes, sizeof(opened.session.bytes));
    return 0;
  } catch (...) {
    return -1;
  }
}

extern "C" auto tsugumi_ruby_route_register(const uint8_t* session,
                                            uint8_t* route) -> int {
  try {
    tsugumi_view_session id{};
    std::memcpy(id.bytes, session, sizeof(id.bytes));
    const auto registered = card::display::route_register(id);
    std::memcpy(route, registered.bytes, sizeof(registered.bytes));
    return 0;
  } catch (...) {
    return -1;
  }
}

extern "C" auto tsugumi_ruby_route_unregister(const uint8_t* route) -> int {
  try {
    tsugumi_route id{};
    std::memcpy(id.bytes, route, sizeof(id.bytes));
    card::display::route_unregister(id);
    return 0;
  } catch (...) {
    return -1;
  }
}

extern "C" auto tsugumi_ruby_send(const uint8_t* session, ruby_bytes bytes,
                                  int framed) -> int {
  try {
    tsugumi_view_session id{};
    std::memcpy(id.bytes, session, sizeof(id.bytes));
    const auto* data = reinterpret_cast<const uint8_t*>(bytes.data);
    if (framed) return tsugumi::abi::SendDisplay(id, {data, bytes.size});
    return card::display::send(id, {data, bytes.size});
  } catch (...) {
    return -1;
  }
}

extern "C" auto tsugumi_ruby_cancelled(int* value) -> int {
  try {
    *value = card::cancelled();
    return 0;
  } catch (...) {
    return 1;
  }
}

extern "C" auto tsugumi_ruby_remaining_ms(int64_t* value) -> int {
  try {
    *value = card::remaining_ms();
    return 0;
  } catch (...) {
    return 1;
  }
}

namespace tsugumi::ruby {

using abi::FailureKind;
using Tsugumi::Card::CardFailureT;
using Tsugumi::Card::CardResultT;

auto InvokeRuby(const Json& input) -> Json {
  std::string encoded;
  try {
    encoded = input.dump();
  } catch (const Json::exception& error) {
    abi::Reject(FailureKind::invalid_request, error.what());
  }
  const std::unique_ptr<char, decltype(&std::free)> output(
      tsugumi_ruby_invoke({encoded.data(), encoded.size()}), &std::free);
  if (!output)
    abi::Reject(FailureKind::worker_failed, "Ruby could not run the Card");
  try {
    return Json::parse(output.get());
  } catch (const Json::exception& error) {
    abi::Reject(FailureKind::output_invalid, error.what());
  }
}

auto FailureResult(const Json& failure) -> CardResultT {
  CardResultT result;
  result.failure = std::make_unique<CardFailureT>();
  auto& value = *result.failure;
  const auto kind = failure.at("kind").get<std::string>();
  value.kind = FailureKind::worker_failed;
  for (const auto candidate : Tsugumi::Data::EnumValuesFailureKind()) {
    if (kind == Tsugumi::Data::EnumNameFailureKind(candidate) &&
        candidate != FailureKind::unspecified) {
      value.kind = candidate;
      break;
    }
  }
  value.message = failure.at("message").get<std::string>();
  value.source = failure.value("source", std::string{});
  value.line = failure.value("line", uint32_t{});
  value.cell = failure.value("cell", std::string{});
  return result;
}

}  // namespace tsugumi::ruby

extern "C" auto tsugumi_ruby_recv(int64_t timeout_ms, ruby_view_event* event)
    -> int {
  try {
    const auto received = card::display::recv(timeout_ms);
    using card::display::ReceiveStatus;
    if (received.status == ReceiveStatus::kTimeout) return 1;
    if (received.status == ReceiveStatus::kCancelled) return 2;
    event->kind = received.status == ReceiveStatus::kClose ? 1u : 0u;
    std::memcpy(event->route, received.route.bytes, sizeof(event->route));
    std::memcpy(event->session, received.session.bytes, sizeof(event->session));
    event->size = received.bytes.size();
    event->data = nullptr;
    if (event->size == 0) return 0;
    event->data = static_cast<uint8_t*>(std::malloc(event->size));
    if (!event->data) return -1;
    std::memcpy(event->data, received.bytes.data(), event->size);
    return 0;
  } catch (...) {
    return -1;
  }
}

extern "C" auto tsugumi_ruby_artifact_dir(int keep, uint8_t* id) -> char* {
  try {
    const auto [artifact, path] = card::Artifact::dir(keep != 0);
    std::memcpy(id, artifact.id->bytes()->Data(), 16);
    return strdup(path.c_str());
  } catch (...) {
    return nullptr;
  }
}

extern "C" auto tsugumi_ruby_artifact_upload(const char* path, int keep,
                                             uint8_t* id) -> int {
  try {
    const auto artifact = card::Artifact::upload(path, keep != 0);
    std::memcpy(id, artifact.id->bytes()->Data(), 16);
    return 0;
  } catch (...) {
    return -1;
  }
}

extern "C" auto tsugumi_ruby_artifact_open(const uint8_t* id, char** path)
    -> int {
  try {
    card::Artifact artifact;
    artifact.id = std::make_unique<tsugumi::ArtifactId>(
        flatbuffers::span<const uint8_t, 16>(id, 16));
    *path = strdup(artifact.open().c_str());
    return *path ? 0 : -1;
  } catch (const std::exception& failure) {
    *path = strdup(failure.what());
    return -1;
  } catch (...) {
    *path = nullptr;
    return -1;
  }
}
