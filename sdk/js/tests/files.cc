#include <dlfcn.h>
#include <unistd.h>

#include <cstdio>
#include <string_view>

#include "wire.h"

extern "C" auto check_file_output(tsugumi_bytes bytes) -> void;

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
  tsugumi_call call = {TSUGUMI_ABI_VERSION, make_input(VALID, 8, -1), nullptr,
                       nullptr};
  tsugumi_bytes result;
  CHECK(entry(&call, &result) == 0);
  check_file_output(result);
  for (int mode = 9; mode <= 10; ++mode) {
    call.input = make_input(VALID, mode, -1);
    CHECK(entry(&call, &result) == 0);
    check_card_failure(result, mode);
  }

  int displays = 0;
  const tsugumi_effects effects = {
      [](void* host, const char* view, const uint8_t* bytes, size_t size) {
        CHECK(view == std::string_view("undeclared"));
        CHECK(size == 1 && bytes[0] == 42);
        ++*static_cast<int*>(host);
        return 37;
      },
      nullptr,
      nullptr,
  };
  call = {TSUGUMI_ABI_VERSION, make_input(VALID, 11, -1), &displays, &effects};
  CHECK(entry(&call, &result) == 0);
  check_success(result, 0);
  CHECK(displays == 1);
  CHECK(dlclose(library) == 0);
}
