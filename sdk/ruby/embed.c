#include <dlfcn.h>
#include <pthread.h>
#include <ruby.h>
#include <ruby/encoding.h>
#include <ruby/thread.h>
#include <stdlib.h>
#include <string.h>

#include "bridge.h"

extern int tsugumi_ruby_on_owner(VALUE (*callback)(VALUE), VALUE context);
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
    rb_raise(rb_eTypeError,
             "Expected an integer view mode, flags or dimension");
  const unsigned long long number = NUM2ULL(value);
  if (number > UINT32_MAX)
    rb_raise(rb_eRangeError, "View mode, flags or dimension is out of range");
  return (uint32_t)number;
}

struct open_request {
  const char* view;
  uint32_t mode;
  uint32_t flags;
  uint32_t width;
  uint32_t height;
  uint8_t route[16];
  uint8_t session[16];
  int status;
};

static void* open_without_gvl(void* opaque) {
  struct open_request* request = opaque;
  request->status = tsugumi_ruby_view_open(
      request->view, request->mode, request->flags, request->width,
      request->height, request->route, request->session);
  return NULL;
}

static VALUE view_open(VALUE self, VALUE view, VALUE mode, VALUE flags,
                       VALUE width, VALUE height) {
  (void)self;
  Check_Type(view, T_STRING);
  VALUE name = rb_str_new_frozen(view);
  struct open_request request = {
      .view = StringValueCStr(name),
      .mode = view_number(mode),
      .flags = view_number(flags),
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

static VALUE artifact_dir(VALUE self, VALUE keep) {
  (void)self;
  uint8_t id[16];
  char* path = tsugumi_ruby_artifact_dir(RTEST(keep), id);
  if (!path) rb_raise(rb_eRuntimeError, "Artifact creation failed");
  VALUE directory = rb_utf8_str_new_cstr(path);
  free(path);
  return rb_ary_new_from_args(2, rb_str_new((const char*)id, sizeof(id)),
                              directory);
}

static VALUE artifact_upload(VALUE self, VALUE path, VALUE keep) {
  (void)self;
  uint8_t id[16];
  if (tsugumi_ruby_artifact_upload(StringValueCStr(path), RTEST(keep), id))
    rb_raise(rb_eRuntimeError, "Artifact upload failed");
  return rb_str_new((const char*)id, sizeof(id));
}

static VALUE artifact_open(VALUE self, VALUE id) {
  (void)self;
  Check_Type(id, T_STRING);
  if (RSTRING_LEN(id) != 16)
    rb_raise(rb_eArgError, "Expected a 16-byte Artifact ID");
  char* path = NULL;
  if (tsugumi_ruby_artifact_open((const uint8_t*)RSTRING_PTR(id), &path)) {
    if (!path) rb_raise(rb_eRuntimeError, "Artifact open failed");
    VALUE message = rb_utf8_str_new_cstr(path);
    free(path);
    rb_exc_raise(rb_exc_new_str(rb_eRuntimeError, message));
  }
  VALUE directory = rb_utf8_str_new_cstr(path);
  free(path);
  return directory;
}

static VALUE initialize(VALUE unused) {
  (void)unused;
  VALUE package = rb_module_new();
  VALUE host = rb_define_module_under(package, "TsugumiHost");
  rb_define_singleton_method(host, "view_open", view_open, 5);
  rb_define_singleton_method(host, "route_register", route_register, 1);
  rb_define_singleton_method(host, "route_unregister", route_unregister, 1);
  rb_define_singleton_method(host, "send_bytes", send_bytes, 3);
  rb_define_singleton_method(host, "recv", receive_bytes, 1);
  rb_define_singleton_method(host, "artifact_dir", artifact_dir, 1);
  rb_define_singleton_method(host, "artifact_upload", artifact_upload, 2);
  rb_define_singleton_method(host, "artifact_open", artifact_open, 1);
  rb_define_singleton_method(host, "cancelled", cancelled, 0);
  rb_define_singleton_method(host, "remaining_ms", remaining_ms, 0);
  VALUE gem = rb_const_get(rb_cObject, rb_intern("Gem"));
  rb_funcall(gem, rb_intern("use_paths"), 1,
             rb_str_new_cstr(TSUGUMI_RUBY_GEMS));
  if (TSUGUMI_RUBY_ACTIVATE[0]) rb_require(TSUGUMI_RUBY_ACTIVATE);
  rb_funcall(rb_mKernel, rb_intern("load"), 2,
             rb_utf8_str_new_cstr(TSUGUMI_RUBY_PARAMS), package);
  rb_funcall(rb_mKernel, rb_intern("load"), 2,
             rb_utf8_str_new_cstr(TSUGUMI_RUBY_SDK), package);
  sdk = rb_const_get(package, rb_intern("Tsugumi"));
  rb_gc_register_address(&sdk);
  return Qnil;
}

struct invocation {
  ruby_bytes input;
  char* output;
};

static VALUE dispatch(VALUE opaque) {
  struct invocation* call = (struct invocation*)opaque;
  if (NIL_P(sdk)) initialize(Qnil);
  VALUE result =
      rb_funcall(sdk, rb_intern("_dispatch"), 3,
                 rb_utf8_str_new(call->input.data, (long)call->input.size),
                 rb_utf8_str_new_cstr(TSUGUMI_RUBY_CARD),
                 rb_utf8_str_new_cstr(TSUGUMI_RUBY_SOURCE_PREFIX));
  Check_Type(result, T_STRING);
  call->output = strdup(StringValueCStr(result));
  return Qnil;
}

static int retain_card(void) {
  Dl_info info;
  return dladdr((const void*)tsugumi_ruby_invoke, &info) &&
         dlopen(info.dli_fname, RTLD_NOW | RTLD_LOCAL);
}

char* tsugumi_ruby_invoke(ruby_bytes input) {
  // Ruby background threads may still hold the Card's bridge functions.
  static int retained;
  if (!retained && !(retained = retain_card())) return NULL;
  struct invocation call = {input, NULL};
  if (!tsugumi_ruby_on_owner(dispatch, (VALUE)&call)) {
    free(call.output);
    return NULL;
  }
  return call.output;
}
