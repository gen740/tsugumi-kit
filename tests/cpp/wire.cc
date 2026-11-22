#define CPP_DISPLAY_WIRE
#include "../wire.cc"

#include <cstring>

#include "cases.h"

namespace {

auto CorruptRootOffset(const uint8_t* data, size_t size) -> void {
  CHECK(size >= FLATBUFFERS_MIN_BUFFER_SIZE);
  const auto offset =
      flatbuffers::EndianScalar(static_cast<flatbuffers::uoffset_t>(size));
  std::memcpy(const_cast<uint8_t*>(data), &offset, sizeof(offset));
}

}  // namespace

extern "C" auto make_cpp_input(int test_case, int mode, int fd)
    -> tsugumi_bytes {
  if (test_case <= DUPLICATE_PORT)
    return make_input(static_cast<input_case>(test_case), mode, fd);
  const auto bytes = make_input(VALID, mode, fd);
  const auto& input = Decode<CardInput>(bytes);
  switch (test_case) {
    case CORRUPT_ENVELOPE_OFFSET:
      CorruptRootOffset(bytes.data, bytes.size);
      break;
    case CORRUPT_PARAMS_OFFSET:
      CorruptRootOffset(input.params()->data(), input.params()->size());
      break;
    case CORRUPT_PORT_OFFSET: {
      const auto* port = input.inputs()->Get(0)->bytes();
      CorruptRootOffset(port->data(), port->size());
      break;
    }
    case RESERVED_IDENTIFIERS:
    case MERGE_TEMPERATURE:
    case MERGE_READING:
    case MERGE_BAD_BYTES:
    case MERGE_EMPTY: {
      CardInputT with_port;
      input.UnPackTo(&with_port);
      if (test_case == RESERVED_IDENTIFIERS) {
        using ParamsPort =
            inputs::tsugumi_class_2::tsugumi_template::tsugumi_union;
        ParamsPort::NativeTableType params_port;
        params_port.value = 43;
        AddPort(with_port, "Params", PortKind::FlatBuffer,
                Encode<ParamsPort>(params_port));
      } else {
        using namespace inputs::arrival::tsugumi::merge;
        MergedT merged;
        if (test_case == MERGE_TEMPERATURE || test_case == MERGE_BAD_BYTES) {
          t0::TemperatureT temperature;
          temperature.value = 20.0;
          TemperatureBoxT box;
          box.bytes = test_case == MERGE_BAD_BYTES
                          ? std::vector<uint8_t>{1, 2, 3}
                          : Encode<t0::Temperature>(temperature);
          merged.value.Set(std::move(box));
        }
        if (test_case == MERGE_READING) {
          t1::ReadingT reading;
          reading.name = "sensor";
          ReadingBoxT box;
          box.bytes = Encode<t1::Reading>(reading);
          merged.value.Set(std::move(box));
        }
        AddPort(with_port, "arrival", PortKind::FlatBuffer,
                Encode<Merged>(merged));
      }
      flatbuffers::FlatBufferBuilder builder;
      builder.Finish(CardInput::Pack(builder, &with_port));
      input_buffer = builder.Release();
      break;
    }
  }
  return {input_buffer.data(), input_buffer.size()};
}

extern "C" auto check_cpp_success(tsugumi_bytes result) -> void {
  const auto& decoded = Decode<CardResult>(result);
  CHECK(!decoded.failure() && decoded.outputs() &&
        decoded.outputs()->size() == 2);
  const auto value =
      DecodePort<outputs::number::Number>(*decoded.outputs()->Get(0));
  CHECK(value.number->value == 42);
  CHECK(value.choice.type == outputs::number::Choice::NONE);
  CHECK(value.choices.size() == 1 && value.choices[0].AsText() &&
        *value.choices[0].AsText() == "valid");
  CHECK(value.required_choice.AsNested() &&
        value.required_choice.AsNested()->return_);

  const auto& keyword = *decoded.outputs()->Get(1);
  CHECK(keyword.name()->str() == "switch" &&
        keyword.kind() == PortKind::FlatBuffer);
  using KeywordPort = outputs::tsugumi_concept::tsugumi_constexpr;
  CHECK(DecodePort<KeywordPort>(keyword).value == 44);
}

extern "C" auto check_merge(tsugumi_bytes result, int scenario) -> void {
  if (scenario == MERGE_BAD_BYTES || scenario == MERGE_EMPTY) {
    const auto& failure = Failure(result, FailureKind::invalid_request);
    CHECK(failure.message()->str().find("arrival") != std::string::npos);
    return;
  }
  const auto& decoded = Decode<CardResult>(result);
  CHECK(!decoded.failure() && decoded.outputs() &&
        decoded.outputs()->size() == 2);
  const auto& port = *decoded.outputs()->Get(1);
  CHECK(port.name()->str() == "arrival" && port.kind() == PortKind::FlatBuffer);
  using namespace outputs::arrival::tsugumi::merge;
  const auto merged = DecodePort<Merged>(port);
  if (scenario == MERGE_TEMPERATURE) {
    const auto* box = merged.value.AsTemperatureBox();
    CHECK(box);
    CHECK(Decode<t0::Temperature>({box->bytes.data(), box->bytes.size()})
              .value() == 21.0);
  } else {
    const auto* box = merged.value.AsReadingBox();
    CHECK(box);
    CHECK(Decode<t1::Reading>({box->bytes.data(), box->bytes.size()})
              .name()
              ->str() == "sensor");
  }
}

extern "C" auto check_cpp_failure(tsugumi_bytes result, int mode) -> void {
  switch (mode) {
    case 20:
      CHECK(Failure(result, FailureKind::input_missing).message()->str() ==
            "Missing input unwired");
      break;
    case 13:
      CHECK(Failure(result, FailureKind::worker_failed).message()->str() ==
            "Invalid FailureKind 0: reserved failure");
      break;
    case 14:
      CHECK(Failure(result, FailureKind::worker_failed).message()->str() ==
            "Invalid FailureKind 65535: unknown failure");
      break;
    default:
      check_card_failure(result, mode);
      break;
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
      Sample{"markdown", R"({"text":"2.5"})"},
      Sample{"table", R"({"columns":["n"],"rows":[["1"]]})"},
      Sample{"image",
             R"({"alt":"sample","media_type":"image/png"})",
             {0, 13, 255}},
      Sample{"points",
             R"({"count":1})",
             {0, 0, 128, 63, 0, 0, 128, 63, 0, 0, 128, 63, 86, 52, 18, 0}},
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
    const auto config = ReadDisplayConfig(payload, sample.data.size() != 0);
    CHECK(config == sample.config);
  }
  CHECK(std::ranges::equal(payload, sample.data));
}
