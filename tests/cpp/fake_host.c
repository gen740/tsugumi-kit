#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <time.h>

#include "cases.h"

#define IN_CALL_DISPLAY_COUNT 8
#define main shared_host_main
#include "../fake_host.c"
#undef main

static int cancelled_true(void* host) {
  CHECK(host == &queries);
  ++queries;
  return 1;
}

static int64_t no_deadline(void* host) {
  CHECK(host == &queries);
  ++queries;
  return -1;
}

struct overlap {
  tsugumi_main_fn entry;
  tsugumi_call call;
  tsugumi_bytes result;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int host_fd;
  int released;
  int returned;
};

static int finish_query(void* host) {
  struct overlap* state = host;
  CHECK(write(state->host_fd, "!", 1) == 1);
  CHECK(pthread_mutex_lock(&state->mutex) == 0);
  while (!state->released)
    CHECK(pthread_cond_wait(&state->cond, &state->mutex) == 0);
  CHECK(pthread_mutex_unlock(&state->mutex) == 0);
  return 1;
}

static int64_t finish_deadline_query(void* host) {
  CHECK(finish_query(host) == 1);
  return -1;
}

static void* invoke(void* arg) {
  struct overlap* state = arg;
  CHECK(state->entry(&state->call, &state->result) == 0);
  CHECK(pthread_mutex_lock(&state->mutex) == 0);
  state->returned = 1;
  CHECK(pthread_cond_signal(&state->cond) == 0);
  CHECK(pthread_mutex_unlock(&state->mutex) == 0);
  return NULL;
}

static void check_query_during_return(tsugumi_main_fn entry, int mode) {
  int sockets[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  const tsugumi_effects effects = {NULL, finish_query, finish_deadline_query};
  struct overlap state = {
      .entry = entry,
      .call = {TSUGUMI_ABI_VERSION, make_cpp_input(VALID, mode, sockets[1]),
               &state, &effects},
      .mutex = PTHREAD_MUTEX_INITIALIZER,
      .cond = PTHREAD_COND_INITIALIZER,
      .host_fd = sockets[0],
  };
  pthread_t caller;
  CHECK(pthread_create(&caller, NULL, invoke, &state) == 0);
  char signal;
  // run is about to return and the Card's helper is inside the host callback.
  CHECK(read(sockets[0], &signal, 1) == 1);
  CHECK(pthread_mutex_lock(&state.mutex) == 0);
  struct timespec deadline;
  CHECK(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
  deadline.tv_nsec += 150000000;
  if (deadline.tv_nsec >= 1000000000) {
    ++deadline.tv_sec;
    deadline.tv_nsec -= 1000000000;
  }
  while (!state.returned) {
    const int status =
        pthread_cond_timedwait(&state.cond, &state.mutex, &deadline);
    if (status == ETIMEDOUT) break;
    CHECK(status == 0);
  }
  CHECK(!state.returned);
  state.released = 1;
  CHECK(pthread_cond_signal(&state.cond) == 0);
  CHECK(pthread_mutex_unlock(&state.mutex) == 0);
  CHECK(pthread_join(caller, NULL) == 0);
  check_success(state.result, 0);

  // Join the Card's helper before unloading the Card or running another case.
  tsugumi_call join = {
      .abi_version = TSUGUMI_ABI_VERSION,
      .input = make_cpp_input(VALID, 2, -1),
  };
  CHECK(entry(&join, &state.result) == 0);
  check_success(state.result, 0);
  CHECK(pthread_cond_destroy(&state.cond) == 0);
  CHECK(pthread_mutex_destroy(&state.mutex) == 0);
  CHECK(close(sockets[0]) == 0 && close(sockets[1]) == 0);
}

static int check_tools(const char* card, const char* expected_path) {
  CHECK(getenv("PATH") == NULL);
  void* library = load(card);
  tsugumi_main_fn entry = get_entry(library);
  const int with_tools = expected_path[0] != '\0';
  tsugumi_call call = {.abi_version = TSUGUMI_ABI_VERSION,
                       .input = make_input(VALID, with_tools ? 18 : 19, -1)};
  tsugumi_bytes result;
  CHECK(entry(&call, &result) == 0);
  check_success(result, 0);
  const char* path = getenv("PATH");
  CHECK(with_tools ? path && strcmp(path, expected_path) == 0 : path == NULL);
  CHECK(dlclose(library) == 0);
  puts(with_tools ? "Card ran a declared tool by name"
                  : "Tool-free Card left PATH unset and found no tool");
  return 0;
}

int main(int argc, char** argv) {
  if (argc == 4 && strcmp(argv[1], "--tools") == 0)
    return check_tools(argv[2], argv[3]);
  CHECK(shared_host_main(argc, argv) == 0);

  void* current = load(argv[1]);
  tsugumi_main_fn entry = get_entry(current);
  tsugumi_bytes result;
  tsugumi_call call = {.abi_version = TSUGUMI_ABI_VERSION};
  for (int scenario = CORRUPT_ENVELOPE_OFFSET; scenario <= CORRUPT_PORT_OFFSET;
       ++scenario) {
    call.input = make_cpp_input(scenario, 0, -1);
    CHECK(entry(&call, &result) == 0);
    check_input_failure(result, MALFORMED_ENVELOPE);
  }

  call.input = make_cpp_input(RESERVED_IDENTIFIERS, 0, -1);
  CHECK(entry(&call, &result) == 0);
  check_cpp_success(result);

  for (int scenario = MERGE_TEMPERATURE; scenario <= MERGE_EMPTY; ++scenario) {
    call.input = make_cpp_input(scenario, 0, -1);
    CHECK(entry(&call, &result) == 0);
    check_merge(result, scenario);
  }

  call.input = make_cpp_input(VALID, 20, -1);
  CHECK(entry(&call, &result) == 0);
  check_cpp_failure(result, 20);

  for (int mode = 8; mode <= 14; ++mode) {
    call.input = make_input(VALID, mode, -1);
    CHECK(entry(&call, &result) == 0);
    check_cpp_failure(result, mode);
  }

  const tsugumi_effects effects = {NULL, cancelled_true, no_deadline};
  queries = 0;
  call.effects = &effects;
  call.host = &queries;
  call.input = make_input(VALID, 15, -1);
  CHECK(entry(&call, &result) == 0);
  check_success(result, 0);
  CHECK(queries == 2);

  check_query_during_return(entry, 16);
  check_query_during_return(entry, 17);
  CHECK(dlclose(current) == 0);
  puts("C++ ABI 9 audit regression checks passed");
  return 0;
}
