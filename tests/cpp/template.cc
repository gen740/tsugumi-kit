#include <dlfcn.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "../wire.h"
#include "_inputs_generated.h"
#include "_outputs_generated.h"
#include "_params_generated.h"
#include "tsugumi/protocol/card_generated.h"

namespace {

using Tsugumi::Card::CardInput;
using Tsugumi::Card::CardInputT;
using Tsugumi::Card::CardPortT;
using Tsugumi::Card::CardResult;
using Tsugumi::Data::FailureKind;
using Tsugumi::Data::PortKind;

template <class Table>
auto Encode(const typename Table::NativeTableType& value)
    -> std::vector<uint8_t> {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Table::Pack(builder, &value));
  return {builder.GetBufferPointer(),
          builder.GetBufferPointer() + builder.GetSize()};
}

}  // namespace

auto main(int argc, char** argv) -> int {
  CHECK(argc == 2 || (argc == 3 && std::string_view(argv[2]) == "--unwired"));
  void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) fprintf(stderr, "%s\n", dlerror());
  CHECK(library);
  const auto entry =
      reinterpret_cast<tsugumi_main_fn>(dlsym(library, "tsugumi_main"));
  CHECK(entry);

  CardInputT input;
  parameters::ParamsT params;
  auto port = std::make_unique<CardPortT>();
#ifdef FILTER
  const auto source = std::filesystem::current_path() / "counts.csv";
  std::ofstream(source) << "channel,counts\n1,8\n2,10\n3,17\n";
  params.minimum = 10;
  port->name = "counts";
  port->kind = PortKind::Path;
  port->path = source.native();
#else
  inputs::celsius::thermometer::TemperatureT celsius;
  celsius.value = 20.0;
  port->name = "celsius";
  port->kind = PortKind::FlatBuffer;
  port->bytes = Encode<inputs::celsius::thermometer::Temperature>(celsius);
#endif
  input.params = Encode<parameters::Params>(params);
  const tsugumi_effects effects{nullptr, [](void*) { return 0; }, nullptr};
  auto invoke = [&] {
    const auto bytes = Encode<CardInput>(input);
    const tsugumi_call call{
        TSUGUMI_ABI_VERSION, {bytes.data(), bytes.size()}, nullptr, &effects};
    tsugumi_bytes result;
    CHECK(entry(&call, &result) == 0);
    flatbuffers::Verifier verifier(result.data, result.size);
    CHECK(verifier.VerifyBuffer<CardResult>());
    return flatbuffers::GetRoot<CardResult>(result.data);
  };

  const auto* absent = invoke();
  CHECK(absent->failure() &&
        absent->failure()->kind() == FailureKind::input_missing);
  CHECK(absent->failure()->message()->str() == "Missing input " + port->name);
  if (argc == 3) {
    CHECK(dlclose(library) == 0);
    puts("Unwired C++ starter Card reported the missing input");
    return 0;
  }
  input.inputs.push_back(std::move(port));

  const auto* result = invoke();
  if (result->failure())
    fprintf(stderr, "%s\n", result->failure()->message()->c_str());
  CHECK(!result->failure() && result->outputs());
#ifdef FILTER
  CHECK(result->outputs()->size() == 2);
  const auto* filtered = result->outputs()->Get(0);
  const auto* original = result->outputs()->Get(1);
  CHECK(filtered->kind() == PortKind::Path && !filtered->bytes());
  CHECK(original->kind() == PortKind::Path && !original->bytes());
  CHECK(filtered->name()->str() == "filtered" &&
        filtered->path()->str() ==
            (source.parent_path() / "filtered.csv").native());
  CHECK(original->name()->str() == "original" &&
        original->path()->str() == source.native());
  std::ifstream stream(filtered->path()->str());
  const std::string csv{std::istreambuf_iterator<char>(stream), {}};
  CHECK(csv == "channel,counts\n2,10\n3,17\n");
#else
  CHECK(result->outputs()->size() == 1);
  const auto* port_result = result->outputs()->Get(0);
  CHECK(port_result->name()->str() == "fahrenheit" &&
        port_result->kind() == PortKind::FlatBuffer && !port_result->path());
  const auto* bytes = port_result->bytes();
  CHECK(bytes);
  using Temperature = thermometer::Temperature;
  flatbuffers::Verifier verifier(bytes->data(), bytes->size());
  CHECK(verifier.VerifyBuffer<Temperature>());
  CHECK(flatbuffers::GetRoot<Temperature>(bytes->data())->value() == 68.0);
#endif
  CHECK(dlclose(library) == 0);
  puts("C++ starter Card ran through tsugumi_main");
  return 0;
}
