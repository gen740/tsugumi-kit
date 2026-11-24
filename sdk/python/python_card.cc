#define PY_SSIZE_T_CLEAN
// Python.h sets feature-test macros and must precede system headers.
// clang-format off
#include <Python.h>
// clang-format on

#include "python_card.hh"

#include <dlfcn.h>

#include <algorithm>
#include <memory>
#include <span>
#include <string>

namespace tsugumi::abi {
namespace {

struct DropReference {
  auto operator()(PyObject* object) const -> void { Py_DECREF(object); }
};
using Object = std::unique_ptr<PyObject, DropReference>;

class Gil {
 public:
  Gil() : state_(PyGILState_Ensure()) {}
  ~Gil() { PyGILState_Release(state_); }
  Gil(const Gil&) = delete;
  auto operator=(const Gil&) -> Gil& = delete;

 private:
  PyGILState_STATE state_;
};

class ReleaseGil {
 public:
  ReleaseGil() : state_(PyEval_SaveThread()) {}
  ~ReleaseGil() { PyEval_RestoreThread(state_); }
  ReleaseGil(const ReleaseGil&) = delete;
  auto operator=(const ReleaseGil&) -> ReleaseGil& = delete;

 private:
  PyThreadState* state_;
};

template <class Query, class Convert>
auto QueryHost(Query query, Convert convert, const char* error) -> PyObject* {
  try {
    decltype(query()) value;
    {
      const ReleaseGil release;
      value = query();
    }
    return convert(value);
  } catch (const std::bad_alloc&) {
    return PyErr_NoMemory();
  } catch (...) {
    PyErr_SetString(PyExc_RuntimeError, error);
    return nullptr;
  }
}

template <class Id>
auto ReadViewId(const char* data, Py_ssize_t size, Id& id) -> bool {
  if (size != static_cast<Py_ssize_t>(sizeof(id.bytes))) {
    PyErr_SetString(PyExc_ValueError, "Expected a 16-byte view ID");
    return false;
  }
  std::copy_n(reinterpret_cast<const uint8_t*>(data), size, id.bytes);
  return true;
}

auto OpenView(PyObject*, PyObject* args) -> PyObject* {
  const char* view;
  unsigned int mode, width, height;
  if (!PyArg_ParseTuple(args, "sIII", &view, &mode, &width, &height))
    return nullptr;
  return QueryHost(
      [&] { return card::display::view_open(view, mode, width, height); },
      [](const auto& opened) {
        return Py_BuildValue(
            "y#y#", reinterpret_cast<const char*>(opened.route.bytes),
            static_cast<Py_ssize_t>(sizeof(opened.route.bytes)),
            reinterpret_cast<const char*>(opened.session.bytes),
            static_cast<Py_ssize_t>(sizeof(opened.session.bytes)));
      },
      "The host could not open the view");
}

auto RegisterRoute(PyObject*, PyObject* args) -> PyObject* {
  const char* bytes;
  Py_ssize_t size;
  tsugumi_view_session session{};
  if (!PyArg_ParseTuple(args, "y#", &bytes, &size) ||
      !ReadViewId(bytes, size, session))
    return nullptr;
  return QueryHost([&] { return card::display::route_register(session); },
                   [](const tsugumi_route& route) {
                     return PyBytes_FromStringAndSize(
                         reinterpret_cast<const char*>(route.bytes),
                         sizeof(route.bytes));
                   },
                   "The host could not register the route");
}

auto UnregisterRoute(PyObject*, PyObject* args) -> PyObject* {
  const char* bytes;
  Py_ssize_t size;
  tsugumi_route route{};
  if (!PyArg_ParseTuple(args, "y#", &bytes, &size) ||
      !ReadViewId(bytes, size, route))
    return nullptr;
  return QueryHost(
      [&] {
        card::display::route_unregister(route);
        return true;
      },
      [](bool) -> PyObject* { Py_RETURN_NONE; },
      "The host could not unregister the route");
}

auto Send(PyObject*, PyObject* args) -> PyObject* {
  const char* id;
  const char* data;
  Py_ssize_t id_size, size;
  int framed;
  tsugumi_view_session session{};
  if (!PyArg_ParseTuple(args, "y#y#p", &id, &id_size, &data, &size, &framed) ||
      !ReadViewId(id, id_size, session))
    return nullptr;
  return QueryHost(
      [&] {
        const std::span bytes(reinterpret_cast<const uint8_t*>(data),
                              static_cast<size_t>(size));
        return framed ? tsugumi::abi::SendDisplay(session,
                                                  {bytes.data(), bytes.size()})
                      : card::display::send_raw(session, bytes);
      },
      [](int status) -> PyObject* {
        if (status != 0) {
          PyErr_SetString(PyExc_RuntimeError,
                          "The host rejected the view send");
          return nullptr;
        }
        Py_RETURN_NONE;
      },
      "The host rejected the view send");
}

auto Receive(PyObject*, PyObject* args) -> PyObject* {
  long long timeout;
  if (!PyArg_ParseTuple(args, "L", &timeout)) return nullptr;
  return QueryHost(
      [&] { return card::display::recv(timeout); },
      [](const card::display::Received& result) -> PyObject* {
        using card::display::ReceiveStatus;
        switch (result.status) {
          case ReceiveStatus::kMessage:
          case ReceiveStatus::kClose:
            return Py_BuildValue(
                "sy#y#y#",
                result.status == ReceiveStatus::kMessage ? "message" : "close",
                reinterpret_cast<const char*>(result.route.bytes),
                static_cast<Py_ssize_t>(sizeof(result.route.bytes)),
                reinterpret_cast<const char*>(result.session.bytes),
                static_cast<Py_ssize_t>(sizeof(result.session.bytes)),
                result.bytes.empty()
                    ? ""
                    : reinterpret_cast<const char*>(result.bytes.data()),
                static_cast<Py_ssize_t>(result.bytes.size()));
          case ReceiveStatus::kTimeout:
            return Py_BuildValue("sOOO", "timeout", Py_None, Py_None, Py_None);
          case ReceiveStatus::kCancelled:
            return Py_BuildValue("sOOO", "cancelled", Py_None, Py_None,
                                 Py_None);
        }
        return nullptr;
      },
      "The host rejected the view receive");
}

auto ArtifactCreate(PyObject*, PyObject*) -> PyObject* {
  return QueryHost(
      card::Artifact::dir,
      [](const auto& result) -> PyObject* {
        const auto& [artifact, path] = result;
        const Object data(PyBytes_FromStringAndSize(
            reinterpret_cast<const char*>(artifact.id->bytes()->Data()), 16));
        const Object name(PyUnicode_DecodeFSDefaultAndSize(
            path.c_str(), path.native().size()));
        return data && name ? PyTuple_Pack(2, data.get(), name.get()) : nullptr;
      },
      "The host could not create an Artifact");
}

auto ArtifactOpen(PyObject*, PyObject* args) -> PyObject* {
  const char* bytes;
  Py_ssize_t size;
  if (!PyArg_ParseTuple(args, "y#", &bytes, &size)) return nullptr;
  if (size != 16) {
    PyErr_SetString(PyExc_ValueError, "Expected a 16-byte Artifact ID");
    return nullptr;
  }
  return QueryHost(
      [&] {
        card::Artifact artifact;
        artifact.id =
            std::make_unique<ArtifactId>(flatbuffers::span<const uint8_t, 16>(
                reinterpret_cast<const uint8_t*>(bytes), 16));
        return artifact.open();
      },
      [](const std::filesystem::path& path) {
        return PyUnicode_DecodeFSDefaultAndSize(path.c_str(),
                                                path.native().size());
      },
      "The host could not open the Artifact");
}

auto Cancelled(PyObject*, PyObject*) -> PyObject* {
  return QueryHost(card::cancelled, PyBool_FromLong,
                   "The host cancellation query failed");
}

auto RemainingMs(PyObject*, PyObject*) -> PyObject* {
  return QueryHost(card::remaining_ms, PyLong_FromLongLong,
                   "The host deadline query failed");
}

PyMethodDef methods[] = {
    {"view_open", OpenView, METH_VARARGS, nullptr},
    {"route_register", RegisterRoute, METH_VARARGS, nullptr},
    {"route_unregister", UnregisterRoute, METH_VARARGS, nullptr},
    {"send", Send, METH_VARARGS, nullptr},
    {"recv", Receive, METH_VARARGS, nullptr},
    {"artifact_create", ArtifactCreate, METH_NOARGS, nullptr},
    {"artifact_open", ArtifactOpen, METH_VARARGS, nullptr},
    {"cancelled", Cancelled, METH_NOARGS, nullptr},
    {"remaining_ms", RemainingMs, METH_NOARGS, nullptr},
    {},
};

PyModuleDef module = {PyModuleDef_HEAD_INIT,
                      "_tsugumi_host",
                      nullptr,
                      -1,
                      methods,
                      nullptr,
                      nullptr,
                      nullptr,
                      nullptr};

auto InitHost() -> PyObject* { return PyModule_Create(&module); }

auto ConfigurePython() -> std::string {
  // The worker loads main.so with RTLD_LOCAL. CPython extensions need to
  // resolve interpreter symbols globally for the lifetime of the process.
  if (!dlopen(TSUGUMI_LIBPYTHON, RTLD_NOW | RTLD_GLOBAL)) return dlerror();
  if (PyImport_AppendInittab("_tsugumi_host", InitHost) != 0)
    return "Cannot register the Python host module";

  PyPreConfig pre;
  PyPreConfig_InitIsolatedConfig(&pre);
  pre.utf8_mode = 1;
  auto status = Py_PreInitialize(&pre);
  if (PyStatus_Exception(status))
    return status.err_msg ? status.err_msg : "Cannot pre-initialize Python";

  PyConfig config;
  PyConfig_InitIsolatedConfig(&config);
  config.write_bytecode = 0;
  config.install_signal_handlers = 0;
  config.buffered_stdio = 0;
  status = PyConfig_SetBytesString(&config, &config.home, TSUGUMI_PY_HOME);
  if (!PyStatus_Exception(status)) status = Py_InitializeFromConfig(&config);
  PyConfig_Clear(&config);
  if (PyStatus_Exception(status))
    return status.err_msg ? status.err_msg : "Cannot initialize Python";

  // Process .pth files in the Nix environment at their usual site boundary.
  const Object site(PyImport_ImportModule("site"));
  const Object packages(PyUnicode_DecodeFSDefault(TSUGUMI_PY_PACKAGES));
  const Object configured(
      site && packages
          ? PyObject_CallMethod(site.get(), "addsitedir", "O", packages.get())
          : nullptr);
  if (!configured) {
    PyErr_WriteUnraisable(nullptr);
    return "Cannot initialize the Python package environment";
  }
  // Keep the generated modules and Card source ahead of any .pth additions.
  auto* path = PySys_GetObject("path");
  const char* paths[] = {TSUGUMI_PY_SDK, TSUGUMI_PY_SOURCE, TSUGUMI_PY_WIRE};
  for (Py_ssize_t i = 0; i < static_cast<Py_ssize_t>(std::size(paths)); ++i) {
    const Object item(PyUnicode_DecodeFSDefault(paths[i]));
    if (!item || PyList_Insert(path, i, item.get()) < 0) {
      PyErr_WriteUnraisable(nullptr);
      return "Cannot configure the Python import path";
    }
  }
  return {};
}

auto InitializePython() -> std::string {
  auto error = ConfigurePython();
  // During and between calls, Python threads may take the GIL and display.
  if (Py_IsInitialized()) PyEval_SaveThread();
  return error;
}

auto LoadDispatch() -> PyObject* {
  Object runtime(PyImport_ImportModule("tsugumi._runtime"));
  auto* dispatch =
      runtime ? PyObject_GetAttrString(runtime.get(), "invoke") : nullptr;
  if (!dispatch) PyErr_WriteUnraisable(nullptr);
  return dispatch;
}

auto ToPythonBytes(tsugumi_bytes bytes) -> PyObject* {
  return PyBytes_FromStringAndSize(reinterpret_cast<const char*>(bytes.data),
                                   bytes.size);
}

auto CallDispatch(PyObject* dispatch, const PythonInput& input) -> PyObject* {
  const Object ports(PyDict_New());
  if (!ports) return nullptr;
  for (const auto& [name, entry] : input.Ports()) {
    const Object key(PyUnicode_DecodeUTF8(name.data(), name.size(), nullptr));
    const Object schema(PyUnicode_DecodeUTF8(entry.schema.data(),
                                             entry.schema.size(), nullptr));
    const Object data(ToPythonBytes(entry.bytes));
    const Object value(
        schema && data ? PyTuple_Pack(2, schema.get(), data.get()) : nullptr);
    if (!key || !value ||
        PyDict_SetItem(ports.get(), key.get(), value.get()) < 0)
      return nullptr;
  }
  return PyObject_CallFunctionObjArgs(dispatch, ports.get(), nullptr);
}

auto ReadResult(PyObject* value) -> Tsugumi::Card::CardResultT {
  Tsugumi::Card::CardResultT result;
  if (PyBytes_Check(value)) {
    const Buffer buffer(
        {reinterpret_cast<const uint8_t*>(PyBytes_AS_STRING(value)),
         static_cast<size_t>(PyBytes_GET_SIZE(value))});
    const auto* failure = buffer.Verify<Tsugumi::Card::CardFailure>();
    if (!failure)
      Reject(FailureKind::worker_failed,
             "Invalid CardFailure from the Python SDK");
    result.failure = std::make_unique<Tsugumi::Card::CardFailureT>();
    failure->UnPackTo(result.failure.get());
    return result;
  }
  if (!PyList_Check(value))
    Reject(FailureKind::worker_failed, "Invalid result from the Python SDK");
  for (Py_ssize_t i = 0; i < PyList_GET_SIZE(value); ++i) {
    const char* name;
    PyObject* data;
    if (!PyArg_ParseTuple(PyList_GET_ITEM(value, i), "sO!", &name,
                          &PyBytes_Type, &data)) {
      PyErr_WriteUnraisable(nullptr);
      Reject(FailureKind::worker_failed,
             "Invalid output port from the Python SDK");
    }
    const auto* bytes =
        reinterpret_cast<const uint8_t*>(PyBytes_AS_STRING(data));
    const size_t size = PyBytes_GET_SIZE(data);
    auto port = std::make_unique<Tsugumi::Card::CardPortT>();
    port->name = name;
    port->bytes.assign(bytes, bytes + size);
    result.outputs.push_back(std::move(port));
  }
  return result;
}

}  // namespace

auto RunPython(const PythonInput& input) -> Tsugumi::Card::CardResultT {
  static const auto initialization_error = InitializePython();
  if (!initialization_error.empty())
    Reject(FailureKind::worker_failed, initialization_error);

  const Gil gil;
  // The interpreter and its dispatch function belong to the worker process.
  static auto* const dispatch = LoadDispatch();
  if (!dispatch)
    Reject(FailureKind::worker_failed, "Cannot load the Python Card SDK");

  const Object value(CallDispatch(dispatch, input));
  if (!value) {
    PyErr_WriteUnraisable(nullptr);
    Reject(FailureKind::worker_failed, "Cannot run the Python Card adapter");
  }
  return ReadResult(value.get());
}

}  // namespace tsugumi::abi
