#include "wire.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "_outputs_generated.h"
#include "adapter.hh"

namespace {

using namespace Tsugumi::Card;
using tsugumi::abi9::Buffer;
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
  CHECK(port->name()->str() == "file" && port->kind() == PortKind::File);
  const auto* data = port->bytes();
  const Buffer payload({data->data(), data->size()});
  const auto* file = payload.Verify<outputs::file::File>();
  CHECK(file && file->value()->str() == "result.txt");
  std::ifstream stream(file->value()->str());
  std::string contents;
  std::getline(stream, contents);
  CHECK(contents == "42");
}
