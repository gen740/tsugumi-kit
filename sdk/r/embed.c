/* Keep R's non-local error handling on the C side of the bridge. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#elif defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#define R_NO_REMAP
#define CSTACK_DEFNS
#include <R_ext/Rdynload.h>
#include <R_ext/Utils.h>
#include <Rconfig.h>
#include <Rembedded.h>
#include <Rinterface.h>
#include <Rinternals.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tsugumi/abi/card_abi.h"

extern bool tsugumi_r_cancelled(void);
extern double tsugumi_r_remaining_ms(void);
extern bool tsugumi_r_display(const char* request, int* status);
extern int tsugumi_r_view_open(const char* view, uint32_t mode, uint32_t width,
                               uint32_t height, tsugumi_route* route,
                               tsugumi_view_session* session);
extern int tsugumi_r_route_register(const tsugumi_view_session* session,
                                    tsugumi_route* route);
extern int tsugumi_r_route_unregister(const tsugumi_route* route);
extern int tsugumi_r_send(const tsugumi_view_session* session,
                          const uint8_t* bytes, size_t size);
/* On success, free event->data.data after consuming it. */
extern int tsugumi_r_recv(int64_t timeout_ms, tsugumi_view_event* event);
char* tsugumi_r_artifact_dir(int keep, uint8_t* id);
int tsugumi_r_artifact_upload(const char* path, int keep, uint8_t* id);
int tsugumi_r_artifact_open(const uint8_t* id, char** path);

static SEXP sdk;

struct call {
  const char* input;
  char* reply;
};

static pthread_once_t startup = PTHREAD_ONCE_INIT;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t request = PTHREAD_COND_INITIALIZER;
static pthread_cond_t response = PTHREAD_COND_INITIALIZER;
static struct call* pending;
static bool started;
static bool boot_failed;

static SEXP cancelled(void) { return Rf_ScalarLogical(tsugumi_r_cancelled()); }

static SEXP remaining_ms(void) {
  return Rf_ScalarReal(tsugumi_r_remaining_ms());
}

static SEXP emit(SEXP request) {
  const char* json = Rf_translateCharUTF8(STRING_ELT(request, 0));
  int status;
  if (!tsugumi_r_display(json, &status))
    Rf_error("could not encode the display value");
  if (status) Rf_error("display rejected");
  return R_NilValue;
}

static void read_view_id(SEXP value, uint8_t (*id)[16]) {
  if (TYPEOF(value) != RAWSXP || XLENGTH(value) != (R_xlen_t)sizeof(*id))
    Rf_error("Expected a 16-byte view ID");
  memcpy(*id, RAW(value), sizeof(*id));
}

static SEXP view_bytes(const uint8_t* bytes, size_t size) {
  SEXP result = Rf_allocVector(RAWSXP, (R_xlen_t)size);
  if (size) memcpy(RAW(result), bytes, size);
  return result;
}

static uint32_t view_option(SEXP value) {
  if ((TYPEOF(value) != INTSXP && TYPEOF(value) != REALSXP) ||
      XLENGTH(value) != 1)
    Rf_error("Expected an unsigned 32-bit view option");
  const double option = Rf_asReal(value);
  if (!R_FINITE(option) || option < 0 || option > UINT32_MAX ||
      option != (double)(uint32_t)option)
    Rf_error("Expected an unsigned 32-bit view option");
  return (uint32_t)option;
}

static SEXP open_view(SEXP view, SEXP mode, SEXP width, SEXP height) {
  if (TYPEOF(view) != STRSXP || XLENGTH(view) != 1 ||
      STRING_ELT(view, 0) == NA_STRING)
    Rf_error("Expected a view name");
  const uint32_t where = view_option(mode);
  const uint32_t w = view_option(width);
  const uint32_t h = view_option(height);
  const char* name = Rf_translateCharUTF8(STRING_ELT(view, 0));
  tsugumi_route route;
  tsugumi_view_session session;
  if (tsugumi_r_view_open(name, where, w, h, &route, &session))
    Rf_error("View open was rejected");
  SEXP result = PROTECT(Rf_allocVector(VECSXP, 2));
  SET_VECTOR_ELT(result, 0, view_bytes(route.bytes, sizeof(route.bytes)));
  SET_VECTOR_ELT(result, 1, view_bytes(session.bytes, sizeof(session.bytes)));
  UNPROTECT(1);
  return result;
}

static SEXP register_route(SEXP value) {
  tsugumi_view_session session;
  read_view_id(value, &session.bytes);
  tsugumi_route route;
  if (tsugumi_r_route_register(&session, &route))
    Rf_error("Route registration was rejected");
  return view_bytes(route.bytes, sizeof(route.bytes));
}

static SEXP unregister_route(SEXP value) {
  tsugumi_route route;
  read_view_id(value, &route.bytes);
  if (tsugumi_r_route_unregister(&route))
    Rf_error("Route unregistration was rejected");
  return R_NilValue;
}

static SEXP send_bytes(SEXP value, SEXP bytes) {
  tsugumi_view_session session;
  read_view_id(value, &session.bytes);
  if (TYPEOF(bytes) != RAWSXP) Rf_error("Expected a raw vector");
  if (tsugumi_r_send(&session, RAW(bytes), (size_t)XLENGTH(bytes)))
    Rf_error("View send was rejected");
  return R_NilValue;
}

/* R uses the registered arity when dispatching the erased function pointer. */
static SEXP utf8(const char* text);

struct view_result {
  int status;
  tsugumi_view_event event;
};

static SEXP pack_view_event(void* opaque) {
  const struct view_result* received = opaque;
  const tsugumi_view_event* event = &received->event;
  const int status = received->status;
  SEXP result = PROTECT(Rf_allocVector(VECSXP, 4));
  const char* kind = status == 0   ? (event->kind == 0 ? "message" : "close")
                     : status == 1 ? "timeout"
                                   : "cancelled";
  SET_VECTOR_ELT(result, 0, utf8(kind));
  if (status == 0) {
    SET_VECTOR_ELT(result, 1,
                   view_bytes(event->route.bytes, sizeof(event->route.bytes)));
    SET_VECTOR_ELT(
        result, 2,
        view_bytes(event->session.bytes, sizeof(event->session.bytes)));
    SET_VECTOR_ELT(result, 3, view_bytes(event->data.data, event->data.size));
  }
  UNPROTECT(1);
  return result;
}

static void free_view_event(void* opaque) {
  const struct view_result* received = opaque;
  free((void*)received->event.data.data);
}

static SEXP receive_event(SEXP timeout) {
  const double millis = Rf_asReal(timeout);
  if (!R_FINITE(millis) || millis < -9007199254740991.0 ||
      millis > 9007199254740991.0 || millis != (double)(int64_t)millis)
    Rf_error("Expected a safe integer timeout");
  struct view_result received = {0};
  received.status = tsugumi_r_recv((int64_t)millis, &received.event);
  if (received.status < 0) Rf_error("View receive was rejected");
  return R_ExecWithCleanup(pack_view_event, &received, free_view_event,
                           &received);
}

static SEXP artifact_dir(SEXP keep) {
  SEXP raw = PROTECT(Rf_allocVector(RAWSXP, 16));
  char* path = tsugumi_r_artifact_dir(Rf_asLogical(keep) == TRUE, RAW(raw));
  if (!path) Rf_error("Artifact creation failed");
  SEXP result = PROTECT(Rf_allocVector(VECSXP, 2));
  SET_VECTOR_ELT(result, 0, raw);
  SET_VECTOR_ELT(result, 1, utf8(path));
  free(path);
  UNPROTECT(2);
  return result;
}

static SEXP artifact_upload(SEXP path, SEXP keep) {
  SEXP raw = PROTECT(Rf_allocVector(RAWSXP, 16));
  if (tsugumi_r_artifact_upload(Rf_translateCharUTF8(STRING_ELT(path, 0)),
                                Rf_asLogical(keep) == TRUE, RAW(raw)))
    Rf_error("Artifact upload failed");
  UNPROTECT(1);
  return raw;
}

static SEXP artifact_open(SEXP id) {
  if (TYPEOF(id) != RAWSXP || XLENGTH(id) != 16)
    Rf_error("Expected a 16-byte Artifact ID");
  char* path = NULL;
  if (tsugumi_r_artifact_open(RAW(id), &path)) {
    if (!path) Rf_error("Artifact open failed");
    SEXP message = PROTECT(utf8(path));
    free(path);
    Rf_error("%s", Rf_translateCharUTF8(STRING_ELT(message, 0)));
  }
  SEXP result = utf8(path);
  free(path);
  return result;
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
static const R_CallMethodDef methods[] = {
    {"tsugumi_r_cancelled", (DL_FUNC)&cancelled, 0},
    {"tsugumi_r_remaining_ms", (DL_FUNC)&remaining_ms, 0},
    {"tsugumi_r_emit", (DL_FUNC)&emit, 1},
    {"tsugumi_r_view_open", (DL_FUNC)&open_view, 4},
    {"tsugumi_r_route_register", (DL_FUNC)&register_route, 1},
    {"tsugumi_r_route_unregister", (DL_FUNC)&unregister_route, 1},
    {"tsugumi_r_send", (DL_FUNC)&send_bytes, 2},
    {"tsugumi_r_recv", (DL_FUNC)&receive_event, 1},
    {"tsugumi_r_artifact_dir", (DL_FUNC)&artifact_dir, 1},
    {"tsugumi_r_artifact_upload", (DL_FUNC)&artifact_upload, 2},
    {"tsugumi_r_artifact_open", (DL_FUNC)&artifact_open, 1},
    {NULL, NULL, 0},
};

static void register_effects(void) {
  R_RegisterCCallable("tsugumi", "view_open", (DL_FUNC)&tsugumi_r_view_open);
  R_RegisterCCallable("tsugumi", "route_register",
                      (DL_FUNC)&tsugumi_r_route_register);
  R_RegisterCCallable("tsugumi", "route_unregister",
                      (DL_FUNC)&tsugumi_r_route_unregister);
  R_RegisterCCallable("tsugumi", "send", (DL_FUNC)&tsugumi_r_send);
  R_RegisterCCallable("tsugumi", "recv", (DL_FUNC)&tsugumi_r_recv);
  R_RegisterCCallable("tsugumi", "cancelled", (DL_FUNC)&tsugumi_r_cancelled);
  R_RegisterCCallable("tsugumi", "remaining_ms",
                      (DL_FUNC)&tsugumi_r_remaining_ms);
}
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static SEXP utf8(const char* text) {
  return Rf_ScalarString(Rf_mkCharCE(text, CE_UTF8));
}

static void source_sdk(SEXP environment, const char* file) {
  SEXP path = PROTECT(utf8(file));
  SEXP load = PROTECT(Rf_lang3(Rf_install("sys.source"), path, environment));
  Rf_eval(load, R_BaseEnv);
  UNPROTECT(2);
}

static void load_sdk(void* unused) {
  (void)unused;
  R_registerRoutines(R_getEmbeddingDllInfo(), NULL, methods, NULL, NULL);
  register_effects();
  SEXP environment = PROTECT(R_NewEnv(R_GlobalEnv, TRUE, 29));
  source_sdk(environment, TSUGUMI_R_SDK);
  source_sdk(environment, TSUGUMI_R_CARD_API);
  source_sdk(environment, TSUGUMI_R_PARAMS);
  R_LockBinding(Rf_install("params"), environment);
  R_PreserveObject(environment);
  sdk = environment;
  UNPROTECT(1);
}

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

static bool initialize(void) {
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
      setenv("R_LIBS_SITE", TSUGUMI_R_LIBS, 1) ||
      setenv("FONTCONFIG_FILE", TSUGUMI_R_FONTS, 1))
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
  return R_ToplevelExec(load_sdk, NULL);
}

static void invoke(void* payload) {
  struct call* call = payload;
  SEXP input = PROTECT(utf8(call->input));
  SEXP path = PROTECT(utf8(TSUGUMI_R_SOURCE));
  SEXP root = PROTECT(utf8(TSUGUMI_R_SOURCE_ROOT));
  SEXP expression =
      PROTECT(Rf_lang4(Rf_install(".tsugumi_invoke"), input, path, root));
  SEXP reply = PROTECT(Rf_eval(expression, sdk));
  call->reply = strdup(Rf_translateCharUTF8(STRING_ELT(reply, 0)));
  UNPROTECT(5);
}

static void* serve(void* unused) {
  (void)unused;
  const bool initialized = initialize();
  pthread_mutex_lock(&mutex);
  if (!initialized) {
    boot_failed = true;
    pending = NULL;
    pthread_cond_signal(&response);
    pthread_mutex_unlock(&mutex);
    return NULL;
  }
  for (;;) {
    while (!pending) pthread_cond_wait(&request, &mutex);
    struct call* call = pending;
    pthread_mutex_unlock(&mutex);

    if (!R_ToplevelExec(invoke, call)) {
      free(call->reply);
      call->reply = NULL;
    }

    pthread_mutex_lock(&mutex);
    pending = NULL;
    pthread_cond_signal(&response);
  }
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

char* tsugumi_r_run(const char* input) {
  pthread_once(&startup, start);
  if (!started) return NULL;

  /* The Card ABI serializes the callers. libR and its owner live until process
   * exit.
   */
  struct call call = {input, NULL};
  pthread_mutex_lock(&mutex);
  if (!boot_failed) {
    pending = &call;
    pthread_cond_signal(&request);
    while (pending) pthread_cond_wait(&response, &mutex);
  }
  pthread_mutex_unlock(&mutex);
  return call.reply;
}
