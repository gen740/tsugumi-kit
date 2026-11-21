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
    case RESERVED_IDENTIFIERS: {
      CardInputT with_params_port;
      input.UnPackTo(&with_params_port);
      using ParamsPort =
          inputs::tsugumi_class_2::tsugumi_template::tsugumi_union;
      ParamsPort::NativeTableType params_port;
      params_port.value = 43;
      AddPort(with_params_port, "Params", PortKind::FlatBuffer,
              Encode<ParamsPort>(params_port));
      flatbuffers::FlatBufferBuilder builder;
      builder.Finish(CardInput::Pack(builder, &with_params_port));
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

extern "C" auto check_cpp_failure(tsugumi_bytes result, int mode) -> void {
  switch (mode) {
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
