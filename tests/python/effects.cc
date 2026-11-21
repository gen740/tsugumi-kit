#include <dlfcn.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <stdexcept>

#include "wire.h"

auto MakeInput(input_case test_case, int mode, int fd, int ack_fd)
    -> tsugumi_bytes;

namespace {

struct QueryPipes {
  std::array<int, 2> start;
  std::array<int, 2> ack;
};

auto WaitForPython(void* host) -> void {
  const auto& pipes = *static_cast<QueryPipes*>(host);
  CHECK(write(pipes.start[1], "!", 1) == 1);
  char signal;
  // The Python thread must acquire the GIL to acknowledge this request.
  CHECK(read(pipes.ack[0], &signal, 1) == 1 && signal == '!');
}

}  // namespace

auto main(int argc, char** argv) -> int {
  CHECK(argc == 2);
  alarm(30);
  void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    std::fprintf(stderr, "%s\n", dlerror());
    return 1;
  }
  const auto entry =
      reinterpret_cast<tsugumi_main_fn>(dlsym(library, "tsugumi_main"));
  CHECK(entry);

  tsugumi_call call = {.abi_version = TSUGUMI_ABI_VERSION,
                       .input = {},
                       .host = nullptr,
                       .effects = nullptr};
  tsugumi_bytes result;
  for (int mode = 8; mode <= 10; ++mode) {
    call.input = make_input(VALID, mode, -1);
    CHECK(entry(&call, &result) == 0);
    check_card_failure(result, mode);
  }

  QueryPipes pipes;
  CHECK(pipe(pipes.start.data()) == 0 && pipe(pipes.ack.data()) == 0);
  const tsugumi_effects blocking = {
      nullptr,
      [](void* host) {
        WaitForPython(host);
        return 0;
      },
      [](void* host) -> int64_t {
        WaitForPython(host);
        return 500;
      },
  };
  call.host = &pipes;
  call.effects = &blocking;
  call.input = MakeInput(VALID, 11, pipes.start[0], pipes.ack[1]);
  CHECK(entry(&call, &result) == 0);
  check_success(result, 0);
  for (const int fd : pipes.start) CHECK(close(fd) == 0);
  for (const int fd : pipes.ack) CHECK(close(fd) == 0);

  const tsugumi_effects throwing = {
      nullptr,
      [](void*) -> int { throw std::runtime_error("cancelled"); },
      [](void*) -> int64_t { throw std::runtime_error("remaining_ms"); },
  };
  call.host = nullptr;
  call.effects = &throwing;
  call.input = make_input(VALID, 12, -1);
  CHECK(entry(&call, &result) == 0);
  check_success(result, 0);
  puts("ABI 9 Python effects and union checks passed");
}
