#include "adapter.hh"

#include <cstdlib>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "bridge.h"
#include "json_codec.hh"

namespace {

auto AsString(ruby_bytes bytes) -> std::string_view {
  return {bytes.data, bytes.size};
}

}  // namespace

extern "C" auto tsugumi_ruby_display(ruby_bytes module, ruby_bytes config,
                                     const ruby_blob* blobs, size_t count,
                                     int record) -> int {
  try {
    std::vector<card::Blob> data;
    data.reserve(count);
    for (const auto& blob : std::span(blobs, count)) {
      data.push_back({AsString(blob.name),
                      {reinterpret_cast<const uint8_t*>(blob.bytes.data),
                       blob.bytes.size}});
    }
    return card::display(card::Module{AsString(module), AsString(config), data},
                         record != 0);
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

using abi9::FailureKind;
using Tsugumi::Card::CardFailureT;
using Tsugumi::Card::CardResultT;

auto InvokeRuby(const Json& input) -> Json {
  std::string encoded;
  try {
    encoded = input.dump();
  } catch (const Json::exception& error) {
    abi9::Reject(FailureKind::invalid_request, error.what());
  }
  const std::unique_ptr<char, decltype(&std::free)> output(
      tsugumi_ruby_invoke({encoded.data(), encoded.size()}), &std::free);
  if (!output)
    abi9::Reject(FailureKind::worker_failed, "Ruby could not run the Card");
  try {
    return Json::parse(output.get());
  } catch (const Json::exception& error) {
    abi9::Reject(FailureKind::output_invalid, error.what());
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
