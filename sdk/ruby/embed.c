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

static void read_view_id(VALUE value, uint8_t* id) {
  Check_Type(value, T_STRING);
  if (RSTRING_LEN(value) != 16)
    rb_raise(rb_eArgError, "Expected a 16-byte view session or route");
  memcpy(id, RSTRING_PTR(value), 16);
}

static uint32_t view_number(VALUE value) {
  if (!RB_INTEGER_TYPE_P(value))
    rb_raise(rb_eTypeError, "Expected an integer view mode or dimension");
  const unsigned long long number = NUM2ULL(value);
  if (number > UINT32_MAX)
    rb_raise(rb_eRangeError, "View mode or dimension is out of range");
  return (uint32_t)number;
}

struct open_request {
  const char* view;
  uint32_t mode;
  uint32_t width;
  uint32_t height;
  uint8_t route[16];
  uint8_t session[16];
  int status;
};

static void* open_without_gvl(void* opaque) {
  struct open_request* request = opaque;
  request->status =
      tsugumi_ruby_view_open(request->view, request->mode, request->width,
                             request->height, request->route, request->session);
  return NULL;
}

static VALUE view_open(VALUE self, VALUE view, VALUE mode, VALUE width,
                       VALUE height) {
  (void)self;
  Check_Type(view, T_STRING);
  VALUE name = rb_str_new_frozen(view);
  struct open_request request = {
      .view = StringValueCStr(name),
      .mode = view_number(mode),
      .width = view_number(width),
      .height = view_number(height),
      .status = -1,
  };
  rb_thread_call_without_gvl(open_without_gvl, &request, NULL, NULL);
  RB_GC_GUARD(name);
  if (request.status) rb_raise(rb_eRuntimeError, "view open was rejected");
  return rb_ary_new_from_args(2, rb_str_new((const char*)request.route, 16),
                              rb_str_new((const char*)request.session, 16));
}

struct route_request {
  uint8_t session[16];
  uint8_t route[16];
  int status;
};

static void* register_without_gvl(void* opaque) {
  struct route_request* request = opaque;
  request->status =
      tsugumi_ruby_route_register(request->session, request->route);
  return NULL;
}

static VALUE route_register(VALUE self, VALUE session) {
  (void)self;
  struct route_request request = {.status = -1};
  read_view_id(session, request.session);
  rb_thread_call_without_gvl(register_without_gvl, &request, NULL, NULL);
  if (request.status)
    rb_raise(rb_eRuntimeError, "view route registration was rejected");
  return rb_str_new((const char*)request.route, 16);
}

static void* unregister_without_gvl(void* opaque) {
  struct route_request* request = opaque;
  request->status = tsugumi_ruby_route_unregister(request->route);
  return NULL;
}

static VALUE route_unregister(VALUE self, VALUE route) {
  (void)self;
  struct route_request request = {.status = -1};
  read_view_id(route, request.route);
  rb_thread_call_without_gvl(unregister_without_gvl, &request, NULL, NULL);
  if (request.status)
    rb_raise(rb_eRuntimeError, "view route unregistration was rejected");
  return Qnil;
}

struct send_request {
  uint8_t session[16];
  ruby_bytes payload;
  int framed;
  int status;
};

static void* send_without_gvl(void* opaque) {
  struct send_request* request = opaque;
  request->status =
      tsugumi_ruby_send(request->session, request->payload, request->framed);
  return NULL;
}

static VALUE send_bytes(VALUE self, VALUE session, VALUE payload,
                        VALUE framed) {
  (void)self;
  Check_Type(payload, T_STRING);
  VALUE stable = rb_str_new_frozen(payload);
  struct send_request request = {
      .payload = bytes(stable),
      .framed = RTEST(framed),
      .status = -1,
  };
  read_view_id(session, request.session);
  rb_thread_call_without_gvl(send_without_gvl, &request, NULL, NULL);
  RB_GC_GUARD(stable);
  if (request.status) rb_raise(rb_eRuntimeError, "view message was rejected");
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

struct receive_request {
  int64_t timeout_ms;
  ruby_view_event event;
  int status;
};

static void* receive_without_gvl(void* opaque) {
  struct receive_request* request = opaque;
  request->status = tsugumi_ruby_recv(request->timeout_ms, &request->event);
  return NULL;
}

static VALUE receive_result(VALUE opaque) {
  struct receive_request* request = (struct receive_request*)opaque;
  rb_thread_call_without_gvl(receive_without_gvl, request, NULL, NULL);
  if (request->status < 0)
    rb_raise(rb_eRuntimeError, "view receive was rejected");
  if (request->status != 0) {
    const char* kind = request->status == 1 ? "timeout" : "cancelled";
    return rb_ary_new_from_args(4, ID2SYM(rb_intern(kind)), Qnil, Qnil, Qnil);
  }

  const ruby_view_event* event = &request->event;
  const char* kind = event->kind == 0 ? "message" : "close";
  return rb_ary_new_from_args(
      4, ID2SYM(rb_intern(kind)), rb_str_new((const char*)event->route, 16),
      rb_str_new((const char*)event->session, 16),
      rb_str_new((const char*)event->data, event->size));
}

static VALUE free_receive_data(VALUE opaque) {
  struct receive_request* request = (struct receive_request*)opaque;
  free(request->event.data);
  return Qnil;
}

static VALUE receive_bytes(VALUE self, VALUE timeout) {
  (void)self;
  struct receive_request request = {.timeout_ms = NUM2LL(timeout),
                                    .status = -1};
  return rb_ensure(receive_result, (VALUE)&request, free_receive_data,
                   (VALUE)&request);
}

static VALUE artifact_dir(VALUE self) {
  (void)self;
  uint8_t id[16];
  char* path = tsugumi_ruby_artifact_dir(id);
  if (!path) rb_raise(rb_eRuntimeError, "Artifact creation failed");
  VALUE directory = rb_utf8_str_new_cstr(path);
  free(path);
  return rb_ary_new_from_args(2, rb_str_new((const char*)id, sizeof(id)),
                              directory);
}

static VALUE artifact_upload(VALUE self, VALUE path) {
  (void)self;
  uint8_t id[16];
  if (tsugumi_ruby_artifact_upload(StringValueCStr(path), id))
    rb_raise(rb_eRuntimeError, "Artifact upload failed");
  return rb_str_new((const char*)id, sizeof(id));
}

static VALUE artifact_open(VALUE self, VALUE id) {
  (void)self;
  Check_Type(id, T_STRING);
  if (RSTRING_LEN(id) != 16)
    rb_raise(rb_eArgError, "Expected a 16-byte Artifact ID");
  char* path = tsugumi_ruby_artifact_open((const uint8_t*)RSTRING_PTR(id));
  if (!path) rb_raise(rb_eRuntimeError, "Artifact open failed");
  VALUE directory = rb_utf8_str_new_cstr(path);
  free(path);
  return directory;
}

static VALUE initialize(VALUE unused) {
  (void)unused;
  VALUE host = rb_define_module("TsugumiHost");
  rb_define_singleton_method(host, "view_open", view_open, 4);
  rb_define_singleton_method(host, "route_register", route_register, 1);
  rb_define_singleton_method(host, "route_unregister", route_unregister, 1);
  rb_define_singleton_method(host, "send_bytes", send_bytes, 3);
  rb_define_singleton_method(host, "recv", receive_bytes, 1);
  rb_define_singleton_method(host, "artifact_dir", artifact_dir, 0);
  rb_define_singleton_method(host, "artifact_upload", artifact_upload, 1);
  rb_define_singleton_method(host, "artifact_open", artifact_open, 1);
  rb_define_const(host, "PARAMS_FILE",
                  rb_utf8_str_new_cstr(TSUGUMI_RUBY_PARAMS));
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
