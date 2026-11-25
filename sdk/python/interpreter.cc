#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <dlfcn.h>

#include <cstdio>
#include <memory>
#include <string>

#include "image_path.hh"

namespace {

auto InterpreterDirectory() -> std::filesystem::path {
  return tsugumi::python::ImagePath(
             reinterpret_cast<const void*>(&InterpreterDirectory))
      .parent_path();
}

struct DropReference {
  auto operator()(PyObject* object) const -> void { Py_DECREF(object); }
};
using Object = std::unique_ptr<PyObject, DropReference>;

auto LoadCard(PyObject*, PyObject* args) -> PyObject* {
  const auto bootstrap = InterpreterDirectory() / "tsugumi-bootstrap.py";
  FILE* script = std::fopen(bootstrap.c_str(), "r");
  if (!script)
    return PyErr_SetFromErrnoWithFilename(PyExc_OSError, bootstrap.c_str());
  const Object scope(PyDict_New());
  if (!scope) {
    std::fclose(script);
    return nullptr;
  }
  const Object loaded(PyRun_FileEx(script, bootstrap.c_str(), Py_file_input,
                                   scope.get(), scope.get(), 1));
  if (!loaded) return nullptr;
  auto* dispatch = PyDict_GetItemString(scope.get(), "load");
  if (!dispatch) {
    PyErr_SetString(PyExc_ImportError,
                    "Python Card bootstrap has no load function");
    return nullptr;
  }
  return PyObject_CallObject(dispatch, args);
}

PyMethodDef methods[] = {
    {"load", LoadCard, METH_VARARGS, nullptr},
    {},
};

PyModuleDef host_module = {PyModuleDef_HEAD_INIT,
                           "_tsugumi_host",
                           nullptr,
                           -1,
                           methods,
                           nullptr,
                           nullptr,
                           nullptr,
                           nullptr};

auto InitHost() -> PyObject* { return PyModule_Create(&host_module); }

auto Initialize() -> std::string {
  const auto lib = InterpreterDirectory();
  if (lib.empty()) return "Cannot locate the Python Card runtime";

  // Extensions loaded by CPython must see the same libpython as the Card.
  if (const auto* python = dlsym(RTLD_DEFAULT, "Py_IsInitialized");
      python && python != reinterpret_cast<const void*>(&Py_IsInitialized))
    return "Python Cards in one process must use the same libpython";
  if (!dlopen((lib / TSUGUMI_LIBPYTHON_NAME).c_str(), RTLD_NOW | RTLD_GLOBAL))
    return dlerror();
  if (Py_IsInitialized()) return "Python was initialized outside the Card SDK";
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
  const auto home = lib.parent_path();
  status = PyConfig_SetBytesString(&config, &config.home, home.c_str());
  if (!PyStatus_Exception(status)) {
    const auto executable = home / "bin" / TSUGUMI_PYTHON_NAME;
    status = PyConfig_SetBytesString(&config, &config.executable,
                                     executable.c_str());
  }
  if (!PyStatus_Exception(status)) status = Py_InitializeFromConfig(&config);
  const std::string error =
      PyStatus_Exception(status)
          ? (status.err_msg ? status.err_msg : "Cannot initialize Python")
          : "";
  PyConfig_Clear(&config);
  if (Py_IsInitialized()) PyEval_SaveThread();
  return error;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) auto
tsugumi_python_initialize() -> const char* {
  // Every Card using this Python installation links this same library.
  static const auto error = Initialize();
  return error.c_str();
}

__attribute__((constructor)) static auto StartPython() -> void {
  // The first Card image loads this shared dependency.
  tsugumi_python_initialize();
}
