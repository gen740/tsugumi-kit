// Exercise the same loader, ABI boundary, and effect lifetimes as the C++ SDK.
#define main run_shared_fake_host
#include "shared_fake_host.c"
#undef main

extern void check_number(tsugumi_bytes result, int64_t expected);

static int should_cancel(void* host) { return *(int*)host; }

static int64_t no_deadline(void* host) {
  CHECK(host);
  return -1;
}

int main(int argc, char** argv) {
  const int status = run_shared_fake_host(argc, argv);
  if (status) return status;

  void* library = load(argv[1]);
  tsugumi_main_fn entry = get_entry(library);
  int stop = 1;
  const tsugumi_effects effects = {NULL, should_cancel, no_deadline};
  tsugumi_call call = {
      .abi_version = TSUGUMI_ABI_VERSION, .host = &stop, .effects = &effects};

  tsugumi_bytes result;
  call.input = make_input(VALID, 8, -1);
  CHECK(entry(&call, &result) == 0);
  check_card_failure(result, 8);

  stop = 0;
  CHECK(entry(&call, &result) == 0);
  check_number(result, 42);
  call.input = make_input(VALID, 9, -1);
  check_number(result, 42);  // Borrow remains valid after old input is freed.

  tsugumi_bytes next;
  CHECK(entry(&call, &next) == 0);
  check_number(next, 43);
  CHECK(dlclose(library) == 0);
  puts("Rust ABI 9 fake-host checks passed");
  return 0;
}
