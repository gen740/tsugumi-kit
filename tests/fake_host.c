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
  unsigned messages;
  unsigned events;
  int live_route;
  tsugumi_artifact_id ids[1 + max_created];
  char granted_path[1024];
  char paths[max_created][1024];
};

static const tsugumi_view_session listing_session = {{0x11}};
static const tsugumi_route opener_route = {{0x22}};
static const tsugumi_route registered_route = {{0x33}};
static const tsugumi_view_session background_session = {{0x44}};
static const tsugumi_route background_route = {{0x55}};

static int view_open(void* context, const char* view, uint32_t mode,
                     uint32_t width, uint32_t height, tsugumi_route* route,
                     tsugumi_view_session* session) {
  struct fake_host* host = context;
  if (strcmp(view, "listing") == 0) {
    CHECK(host->in_call && mode == TSUGUMI_VIEW_WINDOW);
    CHECK(width == 640 && height == 480);
    *route = opener_route;
    *session = listing_session;
  } else {
    CHECK(strcmp(view, "background") == 0);
    CHECK(!host->in_call && mode == TSUGUMI_VIEW_PANEL);
    CHECK(width == 0 && height == 0);
    *route = background_route;
    *session = background_session;
  }
  return 0;
}

static int route_register(void* context, const tsugumi_view_session* session,
                          tsugumi_route* route) {
  struct fake_host* host = context;
  CHECK(host->in_call && !host->live_route);
  CHECK(memcmp(session, &listing_session, sizeof(*session)) == 0);
  *route = registered_route;
  host->live_route = 1;
  return 0;
}

static int route_unregister(void* context, const tsugumi_route* route) {
  struct fake_host* host = context;
  CHECK(host->in_call && host->live_route);
  CHECK(memcmp(route, &registered_route, sizeof(*route)) == 0);
  host->live_route = 0;
  return 0;
}

static int send_display(void* context, const tsugumi_view_session* session,
                        const uint8_t* data, size_t size) {
  struct fake_host* host = context;
  if (size > TSUGUMI_MESSAGE_MAX) return -1;
  pthread_mutex_lock(&host->mutex);
  if (memcmp(session, &background_session, sizeof(*session)) == 0) {
    CHECK(!host->in_call && host->background_received);
    CHECK(size == 1 && data[0] == 42);
    host->background_sent = 1;
    pthread_cond_broadcast(&host->changed);
  } else {
    CHECK(host->in_call &&
          memcmp(session, &listing_session, sizeof(*session)) == 0);
    CHECK(host->messages < 2 &&
          size == (host->messages++ ? TSUGUMI_MESSAGE_MAX : 1));
    for (size_t i = 0; i < size; ++i) CHECK(data[i] == 42);
  }
  pthread_mutex_unlock(&host->mutex);
  return 0;
}

static int recv_display(void* context, tsugumi_view_event* out,
                        int64_t timeout_ms) {
  struct fake_host* host = context;
  static const uint8_t payload[] = {42};
  if (timeout_ms == 0) return 1;
  if (timeout_ms > 0) return 2;
  const tsugumi_bytes message = {payload, sizeof(payload)};
  switch (host->events++) {
    case 0:
      CHECK(host->in_call && host->live_route);
      *out =
          (tsugumi_view_event){0, registered_route, listing_session, message};
      return 0;
    case 1:
      CHECK(host->in_call && !host->live_route);
      *out = (tsugumi_view_event){1, opener_route, listing_session, {NULL, 0}};
      return 0;
    default:
      pthread_mutex_lock(&host->mutex);
      while (host->in_call) pthread_cond_wait(&host->changed, &host->mutex);
      host->background_received = 1;
      pthread_mutex_unlock(&host->mutex);
      *out = (tsugumi_view_event){0, opener_route, listing_session, message};
      return 0;
  }
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
      .view_open = view_open,
      .route_register = route_register,
      .route_unregister = route_unregister,
      .send = send_display,
      .recv = recv_display,
      .cancelled = cancelled,
      .remaining_ms = remaining_ms,
      .artifact_create = artifact_create,
      .artifact_open = artifact_open,
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
    host.messages = 0;
    host.events = 0;
    host.live_route = 0;
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
      CHECK(host.messages == 2 && host.events == 3 &&
            host.created == max_created);
    }
    pthread_mutex_unlock(&host.mutex);
  }
  puts(
      "Card ABI checked: Merge, required and optional outputs, artifacts, "
      "sessions, routes, messages, close and process-lifetime effects");
  return 0;
}
