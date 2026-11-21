/* Reuse the C++ fake host's loader, ABI guard-page test and effect callbacks.
 * The fixture below supplies R-specific scenarios through the shared wire API.
 */
#define main cpp_fake_host_main
#include "fake_host.c"
#undef main

static int cancellation_requested(void* host) {
  CHECK(host == &queries);
  ++queries;
  return 1;
}

int main(int argc, char** argv) {
  CHECK(argc == 3);
  alarm(60);
  void* current = load(argv[1]);
  void* legacy = load(argv[2]);
  check_versions(current, legacy);
  CHECK(dlclose(legacy) == 0);
  tsugumi_main_fn entry = get_entry(current);
  tsugumi_effects effects = {display, cancelled, remaining_ms};
  tsugumi_call call = {.abi_version = TSUGUMI_ABI_VERSION,
                       .host = &queries,
                       .effects = &effects};
  tsugumi_bytes result;

  for (enum input_case scenario = MALFORMED_ENVELOPE;
       scenario <= DUPLICATE_PORT; ++scenario) {
    call.input = make_input(scenario, 0, -1);
    CHECK(entry(&call, &result) == 0);
    check_input_failure(result, scenario);
    CHECK(access("loaded", F_OK) != 0);
  }

  call.input = make_input(VALID, 1, -1);
  CHECK(entry(&call, &result) == 0);
  check_success(result, 1);
  CHECK(queries == 2 && atomic_load(&displays) == 3);

  // Replace the input bytes before reading the borrowed first result again.
  call.input = make_input(VALID, 2, -1);
  check_success(result, 1);
  effects.cancelled = cancellation_requested;
  CHECK(remove("scratch/clear-me") == 0);
  CHECK(entry(&call, &result) == 0);
  check_success(result, 2);
  CHECK(queries == 3 && atomic_load(&displays) == 3);

  for (int mode = 3; mode <= 4; ++mode) {
    call.input = make_input(VALID, mode, -1);
    CHECK(entry(&call, &result) == 0);
    check_card_failure(result, mode);
  }
  /* The process owns R for its lifetime, including registered callbacks. */
  puts("R ABI 9 fake-host checks passed");
  return 0;
}
