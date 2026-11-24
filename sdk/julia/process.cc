#include <dlfcn.h>
#include <julia.h>

#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

auto SetEnv(const char* name, const char* value) -> void {
  if (setenv(name, value, 1) != 0)
    throw std::runtime_error(std::string("Cannot set ") + name);
}

class JuliaScope {
 public:
  JuliaScope()
      : ptls_(static_cast<jl_ptls_t>(jl_get_ptls_states())),
        previous_(jl_gc_unsafe_enter(ptls_)) {}
  ~JuliaScope() { jl_gc_unsafe_leave(ptls_, previous_); }

  JuliaScope(const JuliaScope&) = delete;
  auto operator=(const JuliaScope&) -> JuliaScope& = delete;

 private:
  jl_ptls_t ptls_;
  int8_t previous_;
};

class Interpreter {
 public:
  Interpreter() {
    std::thread([this] { Serve(); }).detach();
  }

  auto Run(void (*callback)(void*), void* context) -> void {
    std::packaged_task<void()> task([this, callback, context] {
      if (failure_) std::rethrow_exception(failure_);
      const JuliaScope scope;
      callback(context);
    });
    auto result = task.get_future();
    {
      const std::lock_guard lock(mutex_);
      pending_.push(std::move(task));
    }
    ready_.notify_one();
    result.get();
  }

 private:
  auto Serve() -> void {
    try {
      Prepare();
    } catch (...) {
      failure_ = std::current_exception();
    }
    for (;;) {
      std::packaged_task<void()> task;
      {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [this] { return !pending_.empty(); });
        task = std::move(pending_.front());
        pending_.pop();
      }
      task();
    }
  }

  static auto Prepare() -> void {
    if (!dlopen(TSUGUMI_LIBJULIA, RTLD_NOW | RTLD_GLOBAL))
      throw std::runtime_error(dlerror());
    const char* home = std::getenv("HOME");
    if (!home || !*home)
      throw std::runtime_error("Julia needs a writable HOME");
    const auto depot = std::string(home) + "/.julia:" TSUGUMI_JULIA_DEPOT ":";
    SetEnv("JULIA_DEPOT_PATH", depot.c_str());
    SetEnv("JULIA_PROJECT", TSUGUMI_JULIA_PROJECT);
    SetEnv("JULIA_LOAD_PATH", "@:@stdlib");
    jl_options.startupfile = JL_OPTIONS_STARTUPFILE_OFF;
    if (!std::getenv("JULIA_NUM_THREADS")) SetEnv("JULIA_NUM_THREADS", "2");
    jl_init();
    jl_gc_safe_enter(static_cast<jl_ptls_t>(jl_get_ptls_states()));
  }

  std::mutex mutex_;
  std::condition_variable ready_;
  std::queue<std::packaged_task<void()>> pending_;
  std::exception_ptr failure_;
};

}  // namespace

extern "C" __attribute__((visibility("default"))) auto tsugumi_julia_on_owner(
    void (*callback)(void*), void* context) -> void {
  static auto* const interpreter = new Interpreter;
  interpreter->Run(callback, context);
}
