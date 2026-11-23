#include "host.h"

#include <cstdlib>
#include <cstring>
#include <memory>

#include "adapter.hh"

using tsugumi::abi::FailureKind;
using Tsugumi::Card::CardFailureT;
using Tsugumi::Card::CardResultT;

auto tsugumi_go_failure(void* raw, uint16_t kind, const char* message,
                        size_t message_size, const char* source,
                        size_t source_size, uint32_t line, const char* cell,
                        size_t cell_size) -> int {
  try {
    auto& result = *static_cast<CardResultT*>(raw);
    auto failure = std::make_unique<CardFailureT>();
    failure->kind = static_cast<FailureKind>(kind);
    failure->message.assign(message, message_size);
    failure->source.assign(source, source_size);
    failure->line = line;
    failure->cell.assign(cell, cell_size);
    result.outputs.clear();
    result.failure = std::move(failure);
    return 0;
  } catch (...) {
    return 1;
  }
}

auto tsugumi_go_send(const char* view, const uint8_t* data, size_t size)
    -> int {
  try {
    return tsugumi::abi::SendDisplay(view, {data, size});
  } catch (...) {
    return 1;
  }
}

auto tsugumi_go_cancelled() -> int { return card::cancelled(); }

auto tsugumi_go_remaining_ms() -> int64_t { return card::remaining_ms(); }

auto tsugumi_go_recv(const char* view, int64_t timeout_ms, uint8_t** data,
                     size_t* size) -> int {
  try {
    const auto message = card::display::recv(view, timeout_ms);
    if (message.status == card::display::ReceiveStatus::kTimeout) return 1;
    if (message.status == card::display::ReceiveStatus::kCancelled) return 2;
    *size = message.bytes.size();
    if (*size == 0) return 0;
    *data = static_cast<uint8_t*>(std::malloc(*size));
    if (!*data) return -1;
    std::memcpy(*data, message.bytes.data(), *size);
    return 0;
  } catch (...) {
    return -1;
  }
}

auto tsugumi_go_artifact_dir(uint8_t* id, char** path) -> int {
  try {
    const auto [artifact, directory] = card::Artifact::dir();
    *path = strdup(directory.c_str());
    if (!*path) return -1;
    std::memcpy(id, artifact.id->bytes()->Data(), 16);
    return 0;
  } catch (...) {
    return -1;
  }
}

auto tsugumi_go_artifact_upload(const char* path, uint8_t* id) -> int {
  try {
    const auto artifact = card::Artifact::upload(path);
    std::memcpy(id, artifact.id->bytes()->Data(), 16);
    return 0;
  } catch (...) {
    return -1;
  }
}

auto tsugumi_go_artifact_open(const uint8_t* id, char** path) -> int {
  try {
    card::Artifact artifact;
    artifact.id = std::make_unique<tsugumi::ArtifactId>(
        flatbuffers::span<const uint8_t, 16>(id, 16));
    *path = strdup(artifact.open().c_str());
    return *path ? 0 : -1;
  } catch (...) {
    return -1;
  }
}
