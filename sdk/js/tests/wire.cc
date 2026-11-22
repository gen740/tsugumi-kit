#include "wire.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "adapter.hh"
#include "js_cases.hh"

namespace {

using namespace Tsugumi::Card;
using tsugumi::abi::Buffer;
using Tsugumi::Data::FailureKind;
using Tsugumi::Data::PortKind;

auto ReportFailure(const CardResult& result) -> void {
  if (const auto* failure = result.failure()) {
    const auto* source = failure->source();
    const auto* message = failure->message();
    std::fprintf(stderr, "%s:%u: unexpected Card failure: %s\n",
                 source ? source->c_str() : "<unknown>", failure->line(),
                 message ? message->c_str() : "");
  }
}

auto CheckFailure(tsugumi_bytes bytes, FailureKind kind) -> CardFailureT {
  const Buffer buffer(bytes);
  const auto* result = buffer.Verify<CardResult>();
  CHECK(result && result->failure() && result->failure()->kind() == kind);
  CHECK(!result->outputs() || !result->outputs()->size());
  CardFailureT failure;
  result->failure()->UnPackTo(&failure);
  CHECK(!failure.message.empty());
  return failure;
}

}  // namespace

extern "C" auto check_shared_success(tsugumi_bytes, int) -> void;

extern "C" auto check_success(tsugumi_bytes bytes, int optional_ports) -> void {
  const Buffer buffer(bytes);
  const auto* result = buffer.Verify<CardResult>();
  CHECK(result);
  ReportFailure(*result);
  check_shared_success(bytes, optional_ports);
}

extern "C" auto check_card_failure(tsugumi_bytes result, int mode) -> void {
  if (mode == 3) {
    const auto failure = CheckFailure(result, FailureKind::cancelled);
    CHECK(failure.message == "explicit failure" && failure.cell == "fit");
    CHECK(failure.source == "src/main.mjs" && failure.line > 0);
  } else if (mode == 4) {
    const auto failure = CheckFailure(result, FailureKind::worker_failed);
    CHECK(failure.message == "Error: standard exception");
    CHECK(failure.source == "src/main.mjs" && failure.line > 0);
  } else if (mode == 5) {
    CHECK(CheckFailure(result, FailureKind::worker_failed).message ==
          "rejected value");
  } else if (mode == 12 || mode == 15) {
    const auto failure = CheckFailure(result, FailureKind::input_missing);
    CHECK(failure.message == (mode == 12 ? "Missing input unwired" : "Missing input file"));
    CHECK(failure.source == "src/main.mjs" && failure.line > 0);
  } else {
    CheckFailure(result, FailureKind::output_invalid);
  }
}

extern "C" auto check_display(const char* view, tsugumi_bytes bytes,
                              unsigned index) -> void {
  struct Sample {
    std::string_view view;
    std::string_view config;
    std::vector<uint8_t> data = {};
  };
  const std::array samples{
      Sample{"text", R"({"text":"résumé\n\""})"},
      Sample{"markdown", R"({"text":"**text**"})"},
      Sample{"table", R"({"columns":["n"],"rows":[["1"]]})"},
      Sample{"image",
             R"({"alt":"sample","media_type":"image/png"})",
             {0, 13, 255}},
      Sample{"points",
             R"({"count":1})",
             {0, 0, 128, 63, 0, 0, 0, 64, 0, 0, 64, 64, 86, 52, 18, 0}},
      Sample{"line-chart",
             R"({"series":[{"count":1,"id":"id","label":"line","offset":0}]})",
             {0, 0, 0, 0, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 8, 64}},
      Sample{"custom", {}, {0, 13, 255}},
      Sample{"text", R"({"text":"outside"})"},
  };
  CHECK(index < samples.size());
  const auto& sample = samples[index];
  CHECK(view == sample.view);
  auto payload = std::span(bytes.data, bytes.size);
  if (sample.view != "custom") {
    CHECK(ReadDisplayConfig(payload, !sample.data.empty()) == sample.config);
  }
  CHECK(std::ranges::equal(payload, sample.data));
}

extern "C" auto check_file_output(tsugumi_bytes bytes) -> void {
  const Buffer buffer(bytes);
  const auto* result = buffer.Verify<CardResult>();
  CHECK(result);
  ReportFailure(*result);
  CHECK(!result->failure() && result->outputs());
  CHECK(result->outputs()->size() == 2);
  const auto* port = result->outputs()->Get(1);
  CHECK(port->name()->str() == "file" && port->kind() == PortKind::Path);
  CHECK(!port->bytes() && port->path()->str() == "result.txt");
  std::ifstream stream(port->path()->str());
  std::string contents;
  std::getline(stream, contents);
  CHECK(contents == "42");
}

namespace {

std::vector<uint8_t> merge_input;

template <class Table>
auto Pack(const typename Table::NativeTableType& value)
    -> std::vector<uint8_t> {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Table::Pack(builder, &value));
  return {builder.GetBufferPointer(),
          builder.GetBufferPointer() + builder.GetSize()};
}

}  // namespace

extern "C" auto make_merge_input(int scenario) -> tsugumi_bytes {
  const auto valid = make_input(VALID, 0, -1);
  const Buffer buffer(valid);
  CardInputT input;
  buffer.Verify<CardInput>()->UnPackTo(&input);
  using namespace inputs::arrival::tsugumi::merge;
  MergedT merged;
  if (scenario == MERGE_TEMPERATURE || scenario == MERGE_BAD_BYTES) {
    t0::TemperatureT temperature;
    temperature.value = 20.0;
    TemperatureBoxT box;
    box.bytes = scenario == MERGE_BAD_BYTES ? std::vector<uint8_t>{1, 2, 3}
                                            : Pack<t0::Temperature>(temperature);
    merged.value.Set(std::move(box));
  } else if (scenario == MERGE_READING) {
    t1::ReadingT reading;
    reading.name = "sensor";
    ReadingBoxT box;
    box.bytes = Pack<t1::Reading>(reading);
    merged.value.Set(std::move(box));
  }
  auto port = std::make_unique<CardPortT>();
  port->name = "arrival";
  port->kind = PortKind::FlatBuffer;
  port->bytes = Pack<Merged>(merged);
  input.inputs.push_back(std::move(port));
  merge_input = Pack<CardInput>(input);
  return {merge_input.data(), merge_input.size()};
}

extern "C" auto check_merge(tsugumi_bytes bytes, int scenario) -> void {
  if (scenario == MERGE_BAD_BYTES || scenario == MERGE_EMPTY) {
    const auto failure = CheckFailure(bytes, FailureKind::invalid_request);
    CHECK(failure.message.find("arrival") != std::string::npos);
    return;
  }
  const Buffer buffer(bytes);
  const auto* result = buffer.Verify<CardResult>();
  CHECK(result);
  ReportFailure(*result);
  CHECK(!result->failure() && result->outputs() &&
        result->outputs()->size() == 2);
  const auto* port = result->outputs()->Get(1);
  CHECK(port->name()->str() == "arrival" &&
        port->kind() == PortKind::FlatBuffer && !port->path());
  using namespace outputs::arrival::tsugumi::merge;
  const Buffer payload({port->bytes()->data(), port->bytes()->size()});
  const auto* merged = payload.Verify<Merged>();
  CHECK(merged);
  if (scenario == MERGE_TEMPERATURE) {
    const auto* box = merged->value_as_TemperatureBox();
    CHECK(box && box->bytes());
    const Buffer nested({box->bytes()->data(), box->bytes()->size()});
    const auto* temperature = nested.Verify<t0::Temperature>();
    CHECK(temperature && temperature->value() == 21.0);
  } else {
    const auto* box = merged->value_as_ReadingBox();
    CHECK(box && box->bytes());
    const Buffer nested({box->bytes()->data(), box->bytes()->size()});
    const auto* reading = nested.Verify<t1::Reading>();
    CHECK(reading && reading->name()->str() == "sensor");
  }
}
