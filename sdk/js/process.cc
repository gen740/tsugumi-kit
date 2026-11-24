#include "process.hh"

#include <dlfcn.h>

#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace tsugumi::abi {
namespace {

struct Environment {
  void* context;
  NodeStop stop;
  NodeJoin join;
};

struct Process {
  std::mutex mutex;
  std::vector<Environment> environments;
  std::shared_ptr<node::InitializationResult> initialization;
};

auto State() -> Process& {
  static auto* state = new Process;
  return *state;
}

auto ShutDown() -> void {
  std::vector<Environment> environments;
  {
    const std::lock_guard lock(State().mutex);
    environments.swap(State().environments);
  }
  for (const auto& env : environments) env.stop(env.context);
  bool joined = true;
  for (const auto& env : environments)
    if (!env.join(env.context)) joined = false;
  if (joined) node::TearDownOncePerProcess();
}

auto Initialize() -> node::InitializationResult& {
  if (!dlopen(TSUGUMI_LIBNODE, RTLD_NOW | RTLD_GLOBAL))
    throw std::runtime_error(dlerror());

  using namespace node::ProcessInitializationFlags;
  auto result = node::InitializeOncePerProcess(
      {TSUGUMI_NODE, "--unhandled-rejections=warn"},
      {kDisableNodeOptionsEnv, kNoStdioInitialization, kNoDefaultSignalHandling,
       kNoParseGlobalDebugVariables, kNoAdjustResourceLimits,
       kNoUseLargePages});
  if (result->early_return()) {
    const auto& errors = result->errors();
    throw std::runtime_error(errors.empty() ? "Cannot initialize Node.js"
                                            : errors.front());
  }
  if (std::atexit(ShutDown) != 0) {
    node::TearDownOncePerProcess();
    throw std::runtime_error("Cannot register the Node.js exit handler");
  }
  State().initialization = std::move(result);
  return *State().initialization;
}

}  // namespace

auto NodeProcess() -> node::InitializationResult& {
  static auto& initialized = Initialize();
  return initialized;
}

auto RegisterNodeEnvironment(void* context, NodeStop stop, NodeJoin join)
    -> void {
  auto& state = State();
  const std::lock_guard lock(state.mutex);
  state.environments.push_back({context, stop, join});
}

}  // namespace tsugumi::abi
