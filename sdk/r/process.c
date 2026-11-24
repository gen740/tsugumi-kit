/* libR must be called on its owner thread. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#elif defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#define R_NO_REMAP
#define CSTACK_DEFNS
#include <Rembedded.h>
#include <Rinterface.h>
#include <Rinternals.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct call {
  const char* libraries;
  const char* fonts;
  void (*work)(void*);
  void* context;
  bool ok;
};

static pthread_once_t startup = PTHREAD_ONCE_INIT;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t request = PTHREAD_COND_INITIALIZER;
static pthread_cond_t response = PTHREAD_COND_INITIALIZER;
static struct call* pending;
static bool started;

static bool stack_bounds(uintptr_t* top, size_t* size) {
#ifdef __APPLE__
  *top = (uintptr_t)pthread_get_stackaddr_np(pthread_self());
  *size = pthread_get_stacksize_np(pthread_self());
  return *top && *size;
#else
  pthread_attr_t attr;
  if (pthread_getattr_np(pthread_self(), &attr) != 0) return false;
  void* base;
  const int status = pthread_attr_getstack(&attr, &base, size);
  pthread_attr_destroy(&attr);
  if (status != 0) return false;
  *top = (uintptr_t)base + *size;
  return true;
#endif
}

static bool initialize(const struct call* call) {
  const char* temporary = getenv("TMPDIR");
  struct stat info;
  if (!temporary || temporary[0] != '/' || strpbrk(temporary, " \t\n\r\f\v") ||
      stat(temporary, &info) != 0 || !S_ISDIR(info.st_mode) ||
      access(temporary, W_OK | X_OK) != 0)
    return false;
  uintptr_t stack_top;
  size_t stack_size;
  if (!stack_bounds(&stack_top, &stack_size)) return false;

  /* Packages loaded by embedded R may resolve R symbols globally. */
  if (!dlopen(TSUGUMI_LIBR, RTLD_NOW | RTLD_GLOBAL)) return false;
  if (setenv("R_HOME", TSUGUMI_R_HOME, 1) ||
      setenv("R_LIBS_SITE", call->libraries, 1) ||
      setenv("FONTCONFIG_FILE", call->fonts, 1))
    return false;

  R_SignalHandlers = 0;
  char* args[] = {"tsugumi",      "--silent",       "--no-save",
                  "--no-restore", "--no-site-file", "--no-init-file",
                  "--no-environ", "--gui=none"};
  if (Rf_initialize_R(sizeof(args) / sizeof(args[0]), args) < 0) return false;
  /* R initially measures the process's main stack. Install the actual owner
   * thread's stack bounds before the interpreter starts evaluating code. */
  R_CStackStart = stack_top;
  R_CStackLimit = stack_size;
  R_Interactive = TRUE;
  setup_Rmainloop();
  return true;
}

static void* serve(void* unused) {
  (void)unused;
  const char* libraries = NULL;
  const char* fonts = NULL;
  bool boot_failed = false;
  pthread_mutex_lock(&mutex);
  for (;;) {
    while (!pending) pthread_cond_wait(&request, &mutex);
    struct call* call = pending;
    pthread_mutex_unlock(&mutex);
    if (!libraries) {
      libraries = call->libraries;
      fonts = call->fonts;
      boot_failed = !initialize(call);
    }
    if (!boot_failed && strcmp(libraries, call->libraries) == 0 &&
        strcmp(fonts, call->fonts) == 0)
      call->ok = R_ToplevelExec(call->work, call->context);

    pthread_mutex_lock(&mutex);
    pending = NULL;
    pthread_cond_broadcast(&response);
  }
  return NULL;
}

static void start(void) {
  pthread_attr_t attr;
  if (pthread_attr_init(&attr) != 0) return;
  pthread_t owner;
  /* R Extensions recommends at least 10 MiB for an embedding thread. */
  if (pthread_attr_setstacksize(&attr, 16 * 1024 * 1024) == 0 &&
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) == 0)
    started = pthread_create(&owner, &attr, serve, NULL) == 0;
  pthread_attr_destroy(&attr);
}

__attribute__((visibility("default"))) bool tsugumi_r_on_owner(
    const char* libraries, const char* fonts, void (*work)(void*),
    void* context) {
  pthread_once(&startup, start);
  if (!started) return false;

  struct call call = {libraries, fonts, work, context, false};
  pthread_mutex_lock(&mutex);
  while (pending) pthread_cond_wait(&response, &mutex);
  pending = &call;
  pthread_cond_signal(&request);
  while (pending == &call) pthread_cond_wait(&response, &mutex);
  pthread_mutex_unlock(&mutex);
  return call.ok;
}
