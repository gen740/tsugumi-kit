#include <dlfcn.h>
#include <pthread.h>
#include <ruby.h>
#include <ruby/encoding.h>
#include <ruby/thread.h>
#include <stdlib.h>
#include <string.h>

#include "bridge.h"

static pthread_once_t startup = PTHREAD_ONCE_INIT;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t request = PTHREAD_COND_INITIALIZER;
static pthread_cond_t response = PTHREAD_COND_INITIALIZER;
static int started;
static int boot_failed;
static int pending;
static ruby_bytes input_json;
static char* output_json;
static VALUE sdk = Qnil;

static ruby_bytes bytes(VALUE value) {
  return (ruby_bytes){RSTRING_PTR(value), (size_t)RSTRING_LEN(value)};
}

static VALUE send_bytes(VALUE self, VALUE view, VALUE payload) {
  (void)self;
  Check_Type(view, T_STRING);
  Check_Type(payload, T_STRING);
  const char* name = StringValueCStr(view);
  if (tsugumi_ruby_send(name, bytes(payload)))
    rb_raise(rb_eRuntimeError, "display was rejected");
  return Qnil;
}

static VALUE cancelled(VALUE self) {
  (void)self;
  int value;
  if (tsugumi_ruby_cancelled(&value))
    rb_raise(rb_eRuntimeError, "cancelled callback failed");
  return value ? Qtrue : Qfalse;
}

static VALUE remaining_ms(VALUE self) {
  (void)self;
  int64_t value;
  if (tsugumi_ruby_remaining_ms(&value))
    rb_raise(rb_eRuntimeError, "remaining_ms callback failed");
  return LL2NUM(value);
}

static VALUE initialize(VALUE unused) {
  (void)unused;
  VALUE host = rb_define_module("TsugumiHost");
  rb_define_singleton_method(host, "send_bytes", send_bytes, 2);
  rb_define_singleton_method(host, "cancelled", cancelled, 0);
  rb_define_singleton_method(host, "remaining_ms", remaining_ms, 0);
  rb_enc_set_default_external(rb_enc_from_encoding(rb_utf8_encoding()));
  VALUE gem = rb_const_get(rb_cObject, rb_intern("Gem"));
  rb_funcall(gem, rb_intern("use_paths"), 1,
             rb_str_new_cstr(TSUGUMI_RUBY_GEMS));
  if (TSUGUMI_RUBY_ACTIVATE[0]) rb_require(TSUGUMI_RUBY_ACTIVATE);
  rb_require(TSUGUMI_RUBY_SDK);
  sdk = rb_const_get(rb_cObject, rb_intern("Tsugumi"));
  rb_gc_register_address(&sdk);
  return Qnil;
}

static VALUE dispatch(VALUE unused) {
  (void)unused;
  VALUE result =
      rb_funcall(sdk, rb_intern("_dispatch"), 3,
                 rb_utf8_str_new(input_json.data, (long)input_json.size),
                 rb_utf8_str_new_cstr(TSUGUMI_RUBY_CARD),
                 rb_utf8_str_new_cstr(TSUGUMI_RUBY_SOURCE_PREFIX));
  Check_Type(result, T_STRING);
  /* JSON escapes embedded NULs. Copy the result before leaving MRI. */
  output_json = strdup(StringValueCStr(result));
  return Qnil;
}

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
  rb_protect(initialize, Qnil, &status);
  if (status) rb_set_errinfo(Qnil);
  return status == 0;
}

static void* wait_for_call(void* unused) {
  (void)unused;
  pthread_mutex_lock(&mutex);
  while (!pending) pthread_cond_wait(&request, &mutex);
  pthread_mutex_unlock(&mutex);
  return NULL;
}

static VALUE serve_call(VALUE unused) {
  rb_thread_call_without_gvl(wait_for_call, NULL, NULL, NULL);
  return dispatch(unused);
}

static void* serve(void* unused) {
  (void)unused;
  RUBY_INIT_STACK;
  if (!boot()) {
    pthread_mutex_lock(&mutex);
    boot_failed = 1;
    pthread_cond_signal(&response);
    pthread_mutex_unlock(&mutex);
    return NULL;
  }

  for (;;) {
    int status = 0;
    rb_protect(serve_call, Qnil, &status);
    if (status) rb_set_errinfo(Qnil);
    pthread_mutex_lock(&mutex);
    pending = 0;
    pthread_cond_signal(&response);
    pthread_mutex_unlock(&mutex);
  }
  return NULL;
}

static void start(void) {
  /* Native gems can resolve MRI symbols even when the worker loads our Card
   * with RTLD_LOCAL. Pin the Card while the resident interpreter can use it. */
  Dl_info info;
  if (!dladdr((const void*)tsugumi_ruby_invoke, &info) ||
      !dlopen(info.dli_fname, RTLD_NOW | RTLD_LOCAL) ||
      !dlopen(TSUGUMI_LIBRUBY, RTLD_NOW | RTLD_GLOBAL))
    return;
  pthread_t owner;
  if (pthread_create(&owner, NULL, serve, NULL) != 0) return;
  pthread_detach(owner);
  started = 1;
}

char* tsugumi_ruby_invoke(ruby_bytes input) {
  pthread_once(&startup, start);
  if (!started) return NULL;
  pthread_mutex_lock(&mutex);
  if (boot_failed) {
    pthread_mutex_unlock(&mutex);
    return NULL;
  }
  input_json = input;
  pending = 1;
  pthread_cond_signal(&request);
  while (pending && !boot_failed) pthread_cond_wait(&response, &mutex);
  char* result = output_json;
  output_json = NULL;
  input_json = (ruby_bytes){0};
  pthread_mutex_unlock(&mutex);
  return result;
}
