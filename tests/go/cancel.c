#include <dlfcn.h>
#include <unistd.h>

#include "wire.h"

static int cancelled(void* host) {
  CHECK(host);
  ++*(int*)host;
  return 1;
}

static int64_t remaining_ms(void* host) {
  CHECK(host);
  return -1;
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  alarm(30);
  void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  CHECK(library);
  tsugumi_main_fn entry = (tsugumi_main_fn)dlsym(library, "tsugumi_main");
  CHECK(entry);

  int queries = 0;
  const tsugumi_effects effects = {.cancelled = cancelled,
                                   .remaining_ms = remaining_ms};
  const tsugumi_call call = {.abi_version = TSUGUMI_ABI_VERSION,
                             .input = make_input(VALID, 8, -1),
                             .host = &queries,
                             .effects = &effects};
  tsugumi_bytes result;
  CHECK(entry(&call, &result) == 0);
  CHECK(queries == 1);
  check_card_failure(result, 8);
  return 0;
}
