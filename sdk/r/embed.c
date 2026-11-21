/* Keep R's non-local error handling on the C side of the bridge. */
#define R_NO_REMAP
#include <R_ext/Rdynload.h>
#include <R_ext/Utils.h>
#include <Rembedded.h>
#include <Rinterface.h>
#include <Rinternals.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern bool tsugumi_r_cancelled(void);
extern double tsugumi_r_remaining_ms(void);
extern bool tsugumi_r_display(const char* request, bool record);

static SEXP sdk;

static SEXP cancelled(void) { return Rf_ScalarLogical(tsugumi_r_cancelled()); }

static SEXP remaining_ms(void) {
  return Rf_ScalarReal(tsugumi_r_remaining_ms());
}

static SEXP display(SEXP request, SEXP record) {
  const char* json = Rf_translateCharUTF8(STRING_ELT(request, 0));
  if (!tsugumi_r_display(json, LOGICAL_ELT(record, 0) == TRUE))
    Rf_error("display rejected");
  return R_NilValue;
}

/* R uses the registered arity when dispatching the erased function pointer. */
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
    {"tsugumi_r_display", (DL_FUNC)&display, 2},
    {NULL, NULL, 0},
};
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static SEXP utf8(const char* text) {
  return Rf_ScalarString(Rf_mkCharCE(text, CE_UTF8));
}

static void load_sdk(void* unused) {
  (void)unused;
  R_registerRoutines(R_getEmbeddingDllInfo(), NULL, methods, NULL, NULL);
  SEXP environment = PROTECT(R_NewEnv(R_GlobalEnv, TRUE, 29));
  SEXP path = PROTECT(utf8(TSUGUMI_R_SDK));
  SEXP load = PROTECT(Rf_lang3(Rf_install("sys.source"), path, environment));
  Rf_eval(load, R_BaseEnv);
  R_PreserveObject(environment);
  sdk = environment;
  UNPROTECT(3);
}

static bool initialize(void) {
  /* Packages loaded by embedded R may resolve R symbols globally. */
  if (!dlopen(TSUGUMI_LIBR, RTLD_NOW | RTLD_GLOBAL)) return false;

  const char* home = getenv("HOME");
  const char* temporary = getenv("TMPDIR");
  if (!home || home[0] != '/' || access(home, W_OK | X_OK) != 0) return false;
  char* saved_tmp = temporary ? strdup(temporary) : NULL;
  if (temporary && !saved_tmp) return false;
  if (setenv("R_HOME", TSUGUMI_R_HOME, 1) ||
      setenv("R_LIBS_SITE", TSUGUMI_R_LIBS, 1) ||
      setenv("FONTCONFIG_FILE", TSUGUMI_R_FONTS, 1) ||
      setenv("TMPDIR", home, 1)) {
    free(saved_tmp);
    return false;
  }

  R_SignalHandlers = 0;
  char* args[] = {"tsugumi",      "--silent",       "--no-save",
                  "--no-restore", "--no-site-file", "--no-init-file",
                  "--no-environ"};
  const int initialized =
      Rf_initEmbeddedR(sizeof(args) / sizeof(args[0]), args);
  const int restored =
      saved_tmp ? setenv("TMPDIR", saved_tmp, 1) : unsetenv("TMPDIR");
  free(saved_tmp);
  return initialized >= 0 && restored == 0 && R_ToplevelExec(load_sdk, NULL);
}

struct call {
  const char* input;
  char* reply;
};

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

char* tsugumi_r_run(const char* input) {
  static bool attempted;
  static bool initialized;
  if (!attempted) {
    attempted = true;
    initialized = initialize();
  }
  if (!initialized) return NULL;
  struct call call = {input, NULL};
  if (!R_ToplevelExec(invoke, &call)) {
    free(call.reply);
    return NULL;
  }
  return call.reply;
}
