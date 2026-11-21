#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "_outputs_generated.h"
#include "_params_generated.h"
#include "tsugumi/protocol/card_generated.h"
#include "wire.h"

auto main(int argc, char** argv) -> int {
  CHECK(argc == 3);
  alarm(30);
  const auto* marker = "go-init.marker";
  CHECK(!std::filesystem::exists(marker));
  void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    fprintf(stderr, "%s\n", dlerror());
    return 1;
  }
  CHECK(!dlopen(argv[2], RTLD_LAZY | RTLD_NOLOAD));
  auto entry =
      reinterpret_cast<tsugumi_main_fn>(dlsym(library, "tsugumi_main"));
  CHECK(entry);

  tsugumi_bytes result = {nullptr, 123};
  tsugumi_call call = {};
  call.abi_version = TSUGUMI_ABI_VERSION - 1;
  CHECK(entry(&call, &result) == 1);
  CHECK(result.data == nullptr && result.size == 123);
  CHECK(!dlopen(argv[2], RTLD_LAZY | RTLD_NOLOAD));
  CHECK(!std::filesystem::exists(marker));

  flatbuffers::FlatBufferBuilder params;
  params.Finish(parameters::CreateParams(params, 42));
  flatbuffers::FlatBufferBuilder input;
  const auto param_bytes =
      input.CreateVector(params.GetBufferPointer(), params.GetSize());
  input.Finish(Tsugumi::Card::CreateCardInput(input, {}, param_bytes));
  call.abi_version = TSUGUMI_ABI_VERSION;
  call.input = {input.GetBufferPointer(), input.GetSize()};

  for (int i = 0; i < 2; ++i) {
    CHECK(entry(&call, &result) == 0);
    flatbuffers::Verifier result_verifier(result.data, result.size);
    CHECK(result_verifier.VerifyBuffer<Tsugumi::Card::CardResult>());
    const auto* decoded =
        flatbuffers::GetRoot<Tsugumi::Card::CardResult>(result.data);
    CHECK(!decoded->failure() && decoded->outputs()->size() == 1);
    const auto* port = decoded->outputs()->Get(0);
    CHECK(port->name()->string_view() == "number");
    CHECK(port->kind() == Tsugumi::Data::PortKind::FlatBuffer);
    const auto* bytes = port->bytes();
    std::vector<uint64_t> aligned((bytes->size() + 7) / 8);
    auto* data = reinterpret_cast<uint8_t*>(aligned.data());
    std::copy(bytes->begin(), bytes->end(), data);
    flatbuffers::Verifier value_verifier(data, bytes->size());
    CHECK(value_verifier.VerifyBuffer<outputs::number::Number>());
    CHECK(flatbuffers::GetRoot<outputs::number::Number>(data)->value() == 42);
  }

  std::ifstream file(marker);
  const std::string init_log(std::istreambuf_iterator<char>{file}, {});
  CHECK(init_log == "loaded\n");
}
