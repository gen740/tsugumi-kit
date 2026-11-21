#include "host.h"

#include <memory>

#include "adapter.hh"

using tsugumi::abi9::FailureKind;
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

auto tsugumi_go_display(const uint8_t* data, size_t size, int record) -> int {
  return tsugumi::abi9::SendDisplay({data, size}, record != 0);
}

auto tsugumi_go_cancelled() -> int { return card::cancelled(); }

auto tsugumi_go_remaining_ms() -> int64_t { return card::remaining_ms(); }
