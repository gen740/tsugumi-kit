#define PY_SSIZE_T_CLEAN
// Python.h sets feature-test macros and must precede system headers.
// clang-format off
#include <Python.h>
// clang-format on

#include "python_card.hh"

#include <dlfcn.h>

#include <memory>
#include <string>

namespace tsugumi::abi9 {
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

auto Display(PyObject*, PyObject* args) -> PyObject* {
  const char* module_id;
  const char* config;
  PyObject* entries;
  int record;
  if (!PyArg_ParseTuple(args, "ssO!p", &module_id, &config, &PyTuple_Type,
                        &entries, &record))
    return nullptr;
  int status = 1;
  try {
    std::vector<card::Blob> blobs;
    const auto count = PyTuple_GET_SIZE(entries);
    blobs.reserve(count);
    for (Py_ssize_t i = 0; i < count; ++i) {
      const char* name;
      PyObject* data;
      if (!PyArg_ParseTuple(PyTuple_GET_ITEM(entries, i), "sO!", &name,
                            &PyBytes_Type, &data))
        return nullptr;
      blobs.push_back(
          {name,
           {reinterpret_cast<const uint8_t*>(PyBytes_AS_STRING(data)),
            static_cast<size_t>(PyBytes_GET_SIZE(data))}});
    }
    // The callback can block while a different Card thread needs the GIL.
    auto* state = PyEval_SaveThread();
    try {
      status = card::display(card::Module{module_id, config, blobs}, record);
    } catch (...) {
      PyEval_RestoreThread(state);
      throw;
    }
    PyEval_RestoreThread(state);
  } catch (const std::bad_alloc&) {
    return PyErr_NoMemory();
  } catch (...) {
    PyErr_SetString(PyExc_RuntimeError, "Cannot encode the display");
    return nullptr;
  }
  if (status) {
    PyErr_SetString(PyExc_RuntimeError, "The host rejected the display");
    return nullptr;
  }
  Py_RETURN_NONE;
}

auto Cancelled(PyObject*, PyObject*) -> PyObject* {
  return PyBool_FromLong(card::cancelled());
}

auto RemainingMs(PyObject*, PyObject*) -> PyObject* {
  return PyLong_FromLongLong(card::remaining_ms());
}

PyMethodDef methods[] = {
    {"display", Display, METH_VARARGS, nullptr},
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

  auto* path = PySys_GetObject("path");
  const char* paths[] = {TSUGUMI_PY_SDK, TSUGUMI_PY_SOURCE, TSUGUMI_PY_WIRE,
                         TSUGUMI_PY_PACKAGES};
  for (Py_ssize_t i = 0; i < static_cast<Py_ssize_t>(std::size(paths)); ++i) {
    Object item(PyUnicode_DecodeFSDefault(paths[i]));
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

auto ToPythonBytes(const flatbuffers::Vector<uint8_t>* bytes) -> PyObject* {
  if (!bytes) return PyBytes_FromStringAndSize("", 0);
  return PyBytes_FromStringAndSize(reinterpret_cast<const char*>(bytes->data()),
                                   bytes->size());
}

auto CallDispatch(PyObject* dispatch, const Tsugumi::Card::CardInput& input)
    -> PyObject* {
  const Object params(ToPythonBytes(input.params()));
  const Object ports(PyDict_New());
  if (!params || !ports) return nullptr;
  if (input.inputs()) {
    for (const auto* port : *input.inputs()) {
      const Object key(PyUnicode_DecodeUTF8(port->name()->data(),
                                            port->name()->size(), nullptr));
      const Object value(ToPythonBytes(port->bytes()));
      if (!key || !value ||
          PyDict_SetItem(ports.get(), key.get(), value.get()) < 0)
        return nullptr;
    }
  }
  return PyObject_CallFunctionObjArgs(dispatch, params.get(), ports.get(),
                                      nullptr);
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
    unsigned char kind;
    PyObject* data;
    if (!PyArg_ParseTuple(PyList_GET_ITEM(value, i), "sBO!", &name, &kind,
                          &PyBytes_Type, &data)) {
      PyErr_WriteUnraisable(nullptr);
      Reject(FailureKind::worker_failed,
             "Invalid output port from the Python SDK");
    }
    const auto* bytes =
        reinterpret_cast<const uint8_t*>(PyBytes_AS_STRING(data));
    auto port = std::make_unique<Tsugumi::Card::CardPortT>();
    port->name = name;
    port->kind = static_cast<PortKind>(kind);
    port->bytes.assign(bytes, bytes + PyBytes_GET_SIZE(data));
    result.outputs.push_back(std::move(port));
  }
  return result;
}

}  // namespace

auto RunPython(tsugumi_bytes bytes) -> Tsugumi::Card::CardResultT {
  const Buffer input_buffer(bytes);
  const auto* input = input_buffer.Verify<Tsugumi::Card::CardInput>();
  if (!input) Reject(FailureKind::invalid_request, "Invalid CardInput");

  static const auto initialization_error = InitializePython();
  if (!initialization_error.empty())
    Reject(FailureKind::worker_failed, initialization_error);

  const Gil gil;
  // The interpreter and its dispatch function belong to the worker process.
  static auto* const dispatch = LoadDispatch();
  if (!dispatch)
    Reject(FailureKind::worker_failed, "Cannot load the Python Card SDK");

  const Object value(CallDispatch(dispatch, *input));
  if (!value) {
    PyErr_WriteUnraisable(nullptr);
    Reject(FailureKind::worker_failed, "Cannot run the Python Card adapter");
  }
  return ReadResult(value.get());
}

}  // namespace tsugumi::abi9
