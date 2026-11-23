#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wire.h"

void check_version(tsugumi_main_fn entry);

enum { max_created = 3 };

struct fake_host {
  pthread_mutex_t mutex;
  pthread_cond_t changed;
  int in_call;
  int background_received;
  int background_sent;
  int granted;
  unsigned next_id;
  unsigned created;
  unsigned chunks;
  unsigned sequence;
  tsugumi_artifact_id ids[1 + max_created];
  char granted_path[1024];
  char paths[max_created][1024];
};

static uint32_t read_u32(const uint8_t* bytes) {
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
         (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static int send_display(void* context, const char* view, const uint8_t* data,
                        size_t size) {
  struct fake_host* host = context;
  CHECK(size >= 16 && size <= TSUGUMI_MESSAGE_MAX);
  CHECK(read_u32(data) == 0x4d475354);
  const uint32_t sequence = read_u32(data + 4);
  const uint32_t index = read_u32(data + 8);
  const uint32_t count = read_u32(data + 12);
  pthread_mutex_lock(&host->mutex);
  if (strcmp(view, "background") == 0) {
    CHECK(!host->in_call && host->background_received);
    CHECK(size == 17 && count == 1 && index == 0 && data[16] == 42);
    host->background_sent = 1;
    pthread_cond_broadcast(&host->changed);
  } else {
    CHECK(host->in_call && strcmp(view, "listing") == 0);
    const unsigned chunk = host->chunks++;
    CHECK(sequence == host->sequence + (chunk != 0));
    CHECK(index == (chunk == 2) && count == (chunk == 0 ? 1u : 2u));
    const size_t sizes[] = {17, TSUGUMI_MESSAGE_MAX, 37};
    CHECK(chunk < 3 && size == sizes[chunk]);
    for (size_t i = 16; i < size; ++i) CHECK(data[i] == 42);
  }
  pthread_mutex_unlock(&host->mutex);
  return 0;
}

static int recv_display(void* context, const char* view, tsugumi_bytes* out,
                        int64_t timeout_ms) {
  struct fake_host* host = context;
  static const uint8_t payload[] = {42};
  if (strcmp(view, "background") == 0) {
    pthread_mutex_lock(&host->mutex);
    while (host->in_call) pthread_cond_wait(&host->changed, &host->mutex);
    host->background_received = 1;
    pthread_mutex_unlock(&host->mutex);
  } else {
    CHECK(host->in_call && strcmp(view, "request") == 0);
    if (timeout_ms == 0) return 1;
    if (timeout_ms > 0) return 2;
  }
  *out = (tsugumi_bytes){payload, sizeof(payload)};
  return 0;
}

static int cancelled(void* context) {
  CHECK(((struct fake_host*)context)->in_call);
  return 0;
}

static int64_t remaining_ms(void* context) {
  CHECK(((struct fake_host*)context)->in_call);
  return 500;
}

static int artifact_create(void* context, tsugumi_artifact_id* id,
                           tsugumi_bytes* path) {
  struct fake_host* host = context;
  CHECK(host->in_call && host->created < max_created);
  const unsigned index = host->created++;
  *id = (tsugumi_artifact_id){{(uint8_t)host->next_id++}};
  host->ids[index + 1] = *id;
  char* buffer = host->paths[index];
  CHECK(getcwd(buffer, sizeof(host->paths[index])));
  const size_t offset = strlen(buffer);
  CHECK(snprintf(buffer + offset, sizeof(host->paths[index]) - offset,
                 "/artifact-%u", (unsigned)id->bytes[0]) > 0);
  CHECK(mkdir(buffer, 0700) == 0);
  *path = (tsugumi_bytes){(const uint8_t*)buffer, strlen(buffer)};
  return 0;
}

static int artifact_open(void* context, const tsugumi_artifact_id* id,
                         tsugumi_bytes* path) {
  struct fake_host* host = context;
  CHECK(host->in_call);
  if (!host->granted || memcmp(id, host->ids, sizeof(*id))) return -1;
  *path = (tsugumi_bytes){(const uint8_t*)host->granted_path,
                          strlen(host->granted_path)};
  return 0;
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  alarm(45);
  void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    fprintf(stderr, "%s\n", dlerror());
    return 1;
  }
  tsugumi_main_fn entry = (tsugumi_main_fn)dlsym(library, "tsugumi_main");
  CHECK(entry);
  check_version(entry);
  const tsugumi_effects effects = {
      send_display, recv_display,    cancelled,
      remaining_ms, artifact_create, artifact_open,
  };
  struct fake_host host = {
      .mutex = PTHREAD_MUTEX_INITIALIZER,
      .changed = PTHREAD_COND_INITIALIZER,
      .next_id = 2,
      .ids = {{{1}}},
  };
  CHECK(getcwd(host.granted_path, sizeof(host.granted_path)));
  strcat(host.granted_path, "/granted");
  CHECK(mkdir(host.granted_path, 0500) == 0);
  for (enum input_case scenario = VALID_NUMBER; scenario <= UNGRANTED_ARTIFACT;
       ++scenario) {
    host.granted = scenario != UNGRANTED_ARTIFACT;
    host.background_received = 0;
    host.background_sent = 0;
    host.created = 0;
    host.chunks = 0;
    host.in_call = 1;
    tsugumi_call call = {TSUGUMI_ABI_VERSION, make_input(scenario), &host,
                         &effects};
    tsugumi_bytes result;
    CHECK(entry(&call, &result) == 0);
    check_result(result, scenario, host.ids, 1 + host.created);
    for (unsigned i = 0; i < host.created; ++i) {
      char file[1200];
      CHECK(snprintf(file, sizeof(file), "%s/value.txt", host.paths[i]) > 0);
      if (i < 2) CHECK(chmod(file, 0400) == 0);
      CHECK(chmod(host.paths[i], 0500) == 0);
    }
    pthread_mutex_lock(&host.mutex);
    host.in_call = 0;
    pthread_cond_broadcast(&host.changed);
    if (scenario <= VALID_TEXT) {
      while (!host.background_sent)
        pthread_cond_wait(&host.changed, &host.mutex);
      CHECK(host.chunks == 3 && host.created == max_created);
      host.sequence += 2;
    }
    pthread_mutex_unlock(&host.mutex);
  }
  puts(
      "Card ABI checked: Merge, required and optional outputs, artifacts, "
      "framed send and process-lifetime recv");
  return 0;
}
