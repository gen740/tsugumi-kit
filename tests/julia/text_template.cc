#include <dlfcn.h>

#include <fstream>
#include <iterator>
#include <string>

#include "_inputs_generated.h"
#include "_params_generated.h"
#include "tsugumi/protocol/card_generated.h"
#include "wire.h"

namespace {

template <class Table>
auto Pack(const typename Table::NativeTableType& value)
    -> std::vector<uint8_t> {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Table::Pack(builder, &value));
  return {builder.GetBufferPointer(),
          builder.GetBufferPointer() + builder.GetSize()};
}

}  // namespace

auto main(int argc, char** argv) -> int {
  using namespace Tsugumi::Card;
  using Tsugumi::Data::FailureKind;
  using Tsugumi::Data::PortKind;

  CHECK(argc == 2);
  auto* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) fprintf(stderr, "%s\n", dlerror());
  CHECK(library);
  const auto entry =
      reinterpret_cast<tsugumi_main_fn>(dlsym(library, "tsugumi_main"));
  CHECK(entry);
  const tsugumi_effects effects{nullptr, [](void*) { return 0; }, nullptr};
  CardInputT input;

  auto invoke = [&] {
    const auto bytes = Pack<CardInput>(input);
    const tsugumi_call call{
        TSUGUMI_ABI_VERSION, {bytes.data(), bytes.size()}, nullptr, &effects};
    tsugumi_bytes result{};
    CHECK(entry(&call, &result) == 0);
    flatbuffers::Verifier verifier(result.data, result.size);
    CHECK(verifier.VerifyBuffer<CardResult>());
    return flatbuffers::GetRoot<CardResult>(result.data);
  };

  const auto* absent = invoke();
  CHECK(absent->failure() &&
        absent->failure()->kind() == FailureKind::input_missing);
  CHECK(absent->failure()->message()->str() == "Missing input text");

  inputs::text::TextT text;
  text.value = "Tsugumi";
  auto port = std::make_unique<CardPortT>();
  port->name = "text";
  port->kind = PortKind::FlatBuffer;
  port->bytes = Pack<inputs::text::Text>(text);
  input.inputs.push_back(std::move(port));

  const auto* result = invoke();
  if (result->failure())
    fprintf(stderr, "%s\n", result->failure()->message()->c_str());
  CHECK(!result->failure() && result->outputs()->size() == 1);
  const auto* output = result->outputs()->Get(0);
  CHECK(output->name()->str() == "file" && output->kind() == PortKind::Path);
  CHECK(!output->bytes() && output->path()->str() == "note.txt");
  std::ifstream file(output->path()->str());
  const std::string content{std::istreambuf_iterator<char>(file), {}};
  CHECK(content == "Tsugumi\n");
  puts("Text starter ran through tsugumi_main");
}
