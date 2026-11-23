#include "adapter.hh"

#include <cstdlib>
#include <cstring>
#include <memory>

#include "bridge.h"
#include "json_codec.hh"

extern "C" auto tsugumi_ruby_send(const char* view, ruby_bytes bytes) -> int {
  try {
    return tsugumi::abi::SendDisplay(
        view, {reinterpret_cast<const uint8_t*>(bytes.data), bytes.size});
  } catch (...) {
    // Never unwind through MRI's C callback.
    return 1;
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

extern "C" auto tsugumi_ruby_recv(const char* view, int64_t timeout_ms,
                                  uint8_t** data, size_t* size) -> int {
  try {
    const auto message = card::display::recv(view, timeout_ms);
    using card::display::ReceiveStatus;
    if (message.status == ReceiveStatus::kTimeout) return 1;
    if (message.status == ReceiveStatus::kCancelled) return 2;
    *size = message.bytes.size();
    *data = nullptr;
    if (*size == 0) return 0;
    *data = static_cast<uint8_t*>(std::malloc(*size));
    if (!*data) return -1;
    std::memcpy(*data, message.bytes.data(), *size);
    return 0;
  } catch (...) {
    return -1;
  }
}

extern "C" auto tsugumi_ruby_artifact_dir(uint8_t* id) -> char* {
  try {
    const auto [artifact, path] = card::Artifact::dir();
    std::memcpy(id, artifact.id->bytes()->Data(), 16);
    return strdup(path.c_str());
  } catch (...) {
    return nullptr;
  }
}

extern "C" auto tsugumi_ruby_artifact_upload(const char* path, uint8_t* id)
    -> int {
  try {
    const auto artifact = card::Artifact::upload(path);
    std::memcpy(id, artifact.id->bytes()->Data(), 16);
    return 0;
  } catch (...) {
    return -1;
  }
}

extern "C" auto tsugumi_ruby_artifact_open(const uint8_t* id) -> char* {
  try {
    card::Artifact artifact;
    artifact.id = std::make_unique<tsugumi::ArtifactId>(
        flatbuffers::span<const uint8_t, 16>(id, 16));
    return strdup(artifact.open().c_str());
  } catch (...) {
    return nullptr;
  }
}
