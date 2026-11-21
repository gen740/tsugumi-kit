// Exercise the same loader, ABI boundary, and effect lifetimes as the C++ SDK.
#define main run_shared_fake_host
#include "shared_fake_host.c"
#undef main

extern void check_number(tsugumi_bytes result, int64_t expected);
extern void check_zero_roots(tsugumi_main_fn entry);

static int should_cancel(void* host) { return *(int*)host; }

static int64_t no_deadline(void* host) {
  CHECK(host);
  return -1;
}

static int reject_display(tsugumi_bytes bytes, int record) {
  CHECK(bytes.data && bytes.size && record == 0);
  return 73;
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

  check_zero_roots(entry);
  for (int mode = 10; mode <= 12; ++mode) {
    call.input = make_input(VALID, mode, -1);
    CHECK(entry(&call, &result) == 0);
    check_card_failure(result, mode);
  }
  const tsugumi_effects rejecting_effects = {reject_display, NULL, NULL};
  call.effects = &rejecting_effects;
  call.input = make_input(VALID, 13, -1);
  CHECK(entry(&call, &result) == 0);
  check_card_failure(result, 13);
  call.input = make_input(VALID, 0, -1);
  CHECK(entry(&call, &result) == 0);
  check_success(result, 0);
  CHECK(dlclose(library) == 0);
  puts("Rust ABI 9 fake-host checks passed");
  return 0;
}
