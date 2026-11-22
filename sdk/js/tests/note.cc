// Runs the js-write-note template once and checks its Path output.
#include <dlfcn.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "_params_generated.h"
#include "adapter.hh"
#include "wire.h"

auto main(int argc, char** argv) -> int {
  CHECK(argc == 2);
  alarm(30);
  auto* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    std::fprintf(stderr, "%s\n", dlerror());
    return 1;
  }
  const auto entry =
      reinterpret_cast<tsugumi_main_fn>(dlsym(library, "tsugumi_main"));
  CHECK(entry);

  parameters::ParamsT params;
  params.text = "Hello from Tsugumi";
  flatbuffers::FlatBufferBuilder params_builder;
  params_builder.Finish(parameters::Params::Pack(params_builder, &params));
  Tsugumi::Card::CardInputT input;
  input.params.assign(
      params_builder.GetBufferPointer(),
      params_builder.GetBufferPointer() + params_builder.GetSize());
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(Tsugumi::Card::CardInput::Pack(builder, &input));

  int displays = 0;
  const tsugumi_effects effects = {
      [](void* host, const char* view, const uint8_t*, size_t) {
        CHECK(view == std::string_view("text"));
        ++*static_cast<int*>(host);
        return 0;
      },
      [](void*) { return 0; },
      [](void*) -> int64_t { return -1; },
  };
  tsugumi_call call = {TSUGUMI_ABI_VERSION,
                       {builder.GetBufferPointer(), builder.GetSize()},
                       &displays, &effects};
  tsugumi_bytes bytes;
  CHECK(entry(&call, &bytes) == 0);
  const tsugumi::abi9::Buffer buffer(bytes);
  const auto* result = buffer.Verify<Tsugumi::Card::CardResult>();
  CHECK(result);
  if (const auto* failure = result->failure())
    std::fprintf(stderr, "Card failure: %s\n", failure->message()->c_str());
  CHECK(!result->failure() && result->outputs());
  CHECK(result->outputs()->size() == 1);
  const auto* port = result->outputs()->Get(0);
  CHECK(port->name()->str() == "file" &&
        port->kind() == Tsugumi::Data::PortKind::Path && !port->bytes());
  std::ifstream file(port->path()->str());
  const std::string contents{std::istreambuf_iterator<char>(file), {}};
  CHECK(contents == "Hello from Tsugumi" && displays == 1);
  CHECK(dlclose(library) == 0);
  std::puts("JavaScript note template checks passed");
}
