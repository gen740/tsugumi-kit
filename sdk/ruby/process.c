#include <dlfcn.h>
#include <pthread.h>
#include <ruby.h>
#include <ruby/encoding.h>
#include <ruby/thread.h>

struct call {
  VALUE (*callback)(VALUE);
  VALUE context;
  int ok;
};

static pthread_once_t startup = PTHREAD_ONCE_INIT;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t request = PTHREAD_COND_INITIALIZER;
static pthread_cond_t response = PTHREAD_COND_INITIALIZER;
static int started;
static int boot_failed;
static struct call* pending;

static int boot(void) {
  int argc = 1;
  static char name[] = "tsugumi-ruby";
  static char* argv_data[] = {name, NULL};
  char** argv = argv_data;
  ruby_sysinit(&argc, &argv);
  /* The caller of boot owns MRI and set its stack base in serve(). */
  if (ruby_setup() != 0) return 0;

  /* Run MRI's builtin prelude, keeping the interpreter resident. */
  char rubyopt[] = "--disable=rubyopt";
  char option[] = "-e", code[] = "";
  char* options[] = {name, rubyopt, option, code, NULL};
  void* program = ruby_options(4, options);
  int status = 0;
  if (!ruby_executable_node(program, &status) || ruby_exec_node(program) != 0)
    return 0;

  ruby_script("tsugumi-card");
  rb_enc_set_default_external(rb_enc_from_encoding(rb_utf8_encoding()));
  return 1;
}

static void* wait_for_call(void* unused) {
  (void)unused;
  pthread_mutex_lock(&mutex);
  while (!pending) pthread_cond_wait(&request, &mutex);
  struct call* call = pending;
  pthread_mutex_unlock(&mutex);
  return call;
}

static void* serve(void* unused) {
  (void)unused;
  RUBY_INIT_STACK;
  if (!boot()) {
    pthread_mutex_lock(&mutex);
    boot_failed = 1;
    pthread_cond_broadcast(&response);
    pthread_mutex_unlock(&mutex);
    return NULL;
  }

  for (;;) {
    struct call* call =
        rb_thread_call_without_gvl(wait_for_call, NULL, NULL, NULL);
    int status = 0;
    rb_protect(call->callback, call->context, &status);
    if (status) rb_set_errinfo(Qnil);

    pthread_mutex_lock(&mutex);
    call->ok = status == 0;
    pending = NULL;
    pthread_cond_broadcast(&response);
    pthread_mutex_unlock(&mutex);
  }
}

static void start(void) {
  if (!dlopen(TSUGUMI_LIBRUBY, RTLD_NOW | RTLD_GLOBAL)) return;
  pthread_t owner;
  if (pthread_create(&owner, NULL, serve, NULL) != 0) return;
  pthread_detach(owner);
  started = 1;
}

__attribute__((visibility("default"))) int tsugumi_ruby_on_owner(
    VALUE (*callback)(VALUE), VALUE context) {
  pthread_once(&startup, start);
  if (!started) return 0;

  struct call call = {callback, context, 0};
  pthread_mutex_lock(&mutex);
  while (pending && !boot_failed) pthread_cond_wait(&response, &mutex);
  if (!boot_failed) {
    pending = &call;
    pthread_cond_signal(&request);
    while (pending == &call && !boot_failed)
      pthread_cond_wait(&response, &mutex);
  }
  pthread_mutex_unlock(&mutex);
  return call.ok;
}
