/* Reuse the C++ fake host's loader, ABI guard-page test and effect callbacks.
 * The fixture below supplies R-specific scenarios through the shared wire API.
 */
#define main cpp_fake_host_main
#include "fake_host.c"
#undef main
#include <pthread.h>

extern void reset_scratch(void);
extern tsugumi_bytes make_bad_merge_input(void);
extern int throwing_cancelled(void*);
extern int64_t throwing_remaining_ms(void*);

static pthread_t r_owner;

static int capture_owner(void* host) {
  if (queries == 0) r_owner = pthread_self();
  return cancelled(host);
}

static int cancellation_requested(void* host) {
  CHECK(host == &queries);
  CHECK(pthread_equal(r_owner, pthread_self()));
  ++queries;
  return 1;
}

struct invocation {
  tsugumi_main_fn entry;
  const tsugumi_call* call;
  tsugumi_bytes* result;
};

static void* invoke_on_another_thread(void* payload) {
  struct invocation* invocation = payload;
  CHECK(!pthread_equal(r_owner, pthread_self()));
  CHECK(invocation->entry(invocation->call, invocation->result) == 0);
  return NULL;
}

int main(int argc, char** argv) {
  CHECK(argc == 3);
  alarm(60);
  void* current = load(argv[1]);
  void* legacy = load(argv[2]);
  check_versions(current, legacy);
  CHECK(dlclose(legacy) == 0);
  tsugumi_main_fn entry = get_entry(current);
  tsugumi_effects effects = {send_display, capture_owner, remaining_ms};
  tsugumi_call call = {.abi_version = TSUGUMI_ABI_VERSION,
                       .host = &queries,
                       .effects = &effects};
  tsugumi_bytes result;

  /* Envelope failures are found before R starts. */
  for (enum input_case scenario = MALFORMED_ENVELOPE;
       scenario <= DUPLICATE_PORT + 1; ++scenario) {
    if (scenario == MISSING_INPUT) continue;
    call.input = scenario > DUPLICATE_PORT ? make_bad_merge_input()
                                           : make_input(scenario, 0, -1);
    CHECK(entry(&call, &result) == 0);
    check_input_failure(result, scenario);
    CHECK(access("loaded", F_OK) != 0);
  }

  int start[2];
  CHECK(pipe(start) == 0 && pipe(displayed) == 0);
  call.input = make_input(VALID, 1, start[0]);
  CHECK(entry(&call, NULL) != 0);
  CHECK(access("loaded", F_OK) != 0);
  uint8_t* storage = malloc(call.input.size + 1);
  CHECK(storage);
  memcpy(storage + 1, call.input.data, call.input.size);
  call.input.data = storage + 1;
  CHECK(entry(&call, &result) == 0);
  free(storage);
  if (atomic_load(&displays) != 10)
    fprintf(stderr, "Received %u of 10 displays\n", atomic_load(&displays));
  check_success(result, 1);
  CHECK(queries == 4 && atomic_load(&displays) == 10);
  CHECK(!pthread_equal(r_owner, pthread_self()));

  // Release the Card's native thread after the call returns.
  call.host = NULL;
  call.effects = NULL;
  CHECK(write(start[1], "!", 1) == 1);
  char signal;
  CHECK(read(displayed[0], &signal, 1) == 1);
  CHECK(queries == 4 && atomic_load(&displays) == 11);

  // Replace the input bytes before reading the borrowed first result again.
  call.input = make_input(OPTIONAL_PORTS, 2, -1);
  check_success(result, 1);
  CHECK(access("scratch/clear-me", F_OK) == 0);
  reset_scratch();
  CHECK(access("scratch/clear-me", F_OK) != 0);
  effects.cancelled = cancellation_requested;
  call.host = &queries;
  call.effects = &effects;
  struct invocation invocation = {entry, &call, &result};
  pthread_t other;
  CHECK(pthread_create(&other, NULL, invoke_on_another_thread, &invocation) ==
        0);
  CHECK(pthread_join(other, NULL) == 0);
  check_success(result, 2);
  CHECK(queries == 5 && atomic_load(&displays) == 11);
  for (int i = 0; i < 2; ++i) {
    CHECK(close(start[i]) == 0 && close(displayed[i]) == 0);
  }

  for (int mode = 3; mode <= 4; ++mode) {
    reset_scratch();
    call.input = make_input(OPTIONAL_PORTS, mode, -1);
    CHECK(entry(&call, &result) == 0);
    check_card_failure(result, mode);
  }

  reset_scratch();
  effects.cancelled = throwing_cancelled;
  effects.remaining_ms = throwing_remaining_ms;
  call.input = make_input(OPTIONAL_PORTS, 5, -1);
  CHECK(entry(&call, &result) == 0);
  check_success(result, 5);

  reset_scratch();
  call.input = make_input(OPTIONAL_PORTS, 6, -1);
  CHECK(entry(&call, &result) == 0);
  check_card_failure(result, 6);

  /* The Card reports the inputs it needs and did not receive. */
  reset_scratch();
  call.input = make_input(OPTIONAL_PORTS, 7, -1);
  CHECK(entry(&call, &result) == 0);
  check_card_failure(result, 7);
  reset_scratch();
  call.input = make_input(MISSING_INPUT, 0, -1);
  CHECK(entry(&call, &result) == 0);
  check_input_failure(result, MISSING_INPUT);
  /* The process owns R for its lifetime, including registered callbacks. */
  puts("R Card ABI fake-host checks passed");
  return 0;
}
