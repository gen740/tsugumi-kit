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

struct display_state {
  unsigned int calls;
  int status;
};

static int receive_display(void* host, const char* view, const uint8_t* bytes,
                           size_t size) {
  CHECK(host && strcmp(view, "undeclared") == 0 && bytes && size == 3);
  CHECK(memcmp(bytes, (uint8_t[]){0, 13, 255}, size) == 0);
  struct display_state* state = host;
  ++state->calls;
  return state->status;
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
  struct display_state display = {0};
  const tsugumi_effects display_effects = {receive_display, NULL, NULL};
  call.host = &display;
  call.effects = &display_effects;
  call.input = make_input(VALID, 13, -1);
  CHECK(entry(&call, &result) == 0);
  check_success(result, 0);
  CHECK(display.calls == 1);

  display.status = 73;
  CHECK(entry(&call, &result) == 0);
  check_card_failure(result, 13);
  CHECK(display.calls == 2);
  for (int mode = 14; mode <= 16; ++mode) {
    call.input = make_input(VALID, mode, -1);
    CHECK(entry(&call, &result) == 0);
    check_card_failure(result, mode);
  }
  CHECK(display.calls == 2);
  call.input = make_input(VALID, 0, -1);
  CHECK(entry(&call, &result) == 0);
  check_success(result, 0);
  CHECK(dlclose(library) == 0);
  puts("Rust ABI 9 fake-host checks passed");
  return 0;
}
