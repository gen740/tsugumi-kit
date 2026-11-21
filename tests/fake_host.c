#include <dlfcn.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "wire.h"

static unsigned int queries;
static _Atomic unsigned int displays;
static int displayed[2];

static int display(tsugumi_bytes data, int record) {
  unsigned int index = atomic_fetch_add(&displays, 1);
  check_display(data, record, index);
  if (index == 10) CHECK(write(displayed[1], "!", 1) == 1);
  return 0;
}

static int cancelled(void* host) {
  CHECK(host == &queries);
  ++queries;
  return 0;
}

static int64_t remaining_ms(void* host) {
  CHECK(host == &queries);
  ++queries;
  return 500;
}

static void* load(const char* path) {
  void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    fprintf(stderr, "%s\n", dlerror());
    abort();
  }
  return library;
}

static tsugumi_main_fn get_entry(void* library) {
  tsugumi_main_fn entry = (tsugumi_main_fn)dlsym(library, "tsugumi_main");
  CHECK(entry);
  return entry;
}

struct abi8_call {
  uint32_t abi_version;
  uint32_t phase;
};

static void check_versions(void* current, void* legacy) {
  typedef int (*abi8_entry_fn)(const struct abi8_call*);
  abi8_entry_fn old_entry = (abi8_entry_fn)dlsym(legacy, "tsugumi_main");
  CHECK(old_entry && old_entry(&(struct abi8_call){8u, 3u}) == 0);
  long page_size = sysconf(_SC_PAGESIZE);
  CHECK(page_size > 0);
  void* pages = mmap(NULL, (size_t)page_size * 2, PROT_READ | PROT_WRITE,
                     MAP_ANON | MAP_PRIVATE, -1, 0);
  CHECK(pages != MAP_FAILED);
  uint8_t* boundary = (uint8_t*)pages + page_size;
  CHECK(mprotect(boundary, (size_t)page_size, PROT_NONE) == 0);
  tsugumi_bytes unchanged = {NULL, 123};

  // The ABI 8 phase and the ABI 9 input both start in the guard page.
  uint32_t* old_version = (uint32_t*)(boundary - sizeof(uint32_t));
  *old_version = TSUGUMI_ABI_VERSION;
  CHECK(old_entry((const struct abi8_call*)old_version) == 1);
  void* new_prefix = boundary - offsetof(tsugumi_call, input);
  memcpy(new_prefix, &(uint32_t){8u}, sizeof(uint32_t));
  const tsugumi_call* call = new_prefix;
  CHECK(get_entry(current)(call, &unchanged) == 1);
  CHECK(unchanged.data == NULL && unchanged.size == 123);
  CHECK(munmap(pages, (size_t)page_size * 2) == 0);
}

int main(int argc, char** argv) {
  CHECK(argc == 3);
  alarm(30);
  void* current = load(argv[1]);
  void* legacy = load(argv[2]);
  check_versions(current, legacy);
  CHECK(dlclose(legacy) == 0);

  tsugumi_main_fn entry = get_entry(current);
  tsugumi_call call = {.abi_version = TSUGUMI_ABI_VERSION};
  tsugumi_bytes result;
  const tsugumi_effects effects = {display, cancelled, remaining_ms};
  call.host = &queries;
  call.effects = &effects;
  for (enum input_case scenario = VALID; scenario <= DUPLICATE_PORT;
       ++scenario) {
    call.input = make_input(scenario, 0, -1);
    CHECK(entry(&call, &result) == 0);
    if (scenario <= OPTIONAL_PORTS)
      check_success(result, scenario == OPTIONAL_PORTS);
    else
      check_input_failure(result, scenario);
  }

  for (int mode = 3; mode <= 7; ++mode) {
    call.input = make_input(VALID, mode, -1);
    CHECK(entry(&call, &result) == 0);
    check_card_failure(result, mode);
  }

  int start[2];
  CHECK(pipe(start) == 0 && pipe(displayed) == 0);
  call.input = make_input(VALID, 1, start[0]);
  CHECK(entry(&call, NULL) != 0);
  uint8_t* storage = malloc(call.input.size + 1);
  CHECK(storage);
  memcpy(storage + 1, call.input.data, call.input.size);
  call.input.data = storage + 1;
  CHECK(entry(&call, &result) == 0);
  free(storage);
  check_success(result, 0);
  CHECK(queries == 2 && atomic_load(&displays) == 10);

  // Release the Card thread after the call and discard the previous host
  // context.
  call.host = NULL;
  call.effects = NULL;
  CHECK(write(start[1], "!", 1) == 1);
  char signal;
  CHECK(read(displayed[0], &signal, 1) == 1);
  CHECK(queries == 2 && atomic_load(&displays) == 11);
  call.input = make_input(VALID, 2, -1);
  check_success(result, 0);  // Still readable after effects and input release.
  CHECK(entry(&call, &result) == 0);
  check_success(result, 0);
  for (int i = 0; i < 2; ++i) {
    CHECK(close(start[i]) == 0);
    CHECK(close(displayed[i]) == 0);
  }
  CHECK(dlclose(current) == 0);
  puts("ABI 9 fake-host checks passed");
  return 0;
}
