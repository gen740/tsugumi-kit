#include "bridge.hh"

#include <dlfcn.h>
#include <julia.h>

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "legacy-display.hh"

namespace tsugumi::julia {
namespace {

extern "C" auto IsCancelled() noexcept -> int {
  try {
    return card::cancelled();
  } catch (...) {
    return 1;
  }
}

extern "C" auto RemainingMs() noexcept -> int64_t {
  try {
    return card::remaining_ms();
  } catch (...) {
    return 0;
  }
}

template <class T>
auto Field(const Json& value, const char* name) -> T {
  return value.at(name).get<T>();
}

auto Emit(std::string_view kind, const Json& value, bool record) -> int {
  if (kind == "text" || kind == "markdown")
    return legacy_display::Emit(kind, {{"text", value.get<std::string>()}},
                                record);
  if (kind == "table") {
    const auto columns = Field<std::vector<std::string>>(value, "columns");
    const auto rows =
        Field<std::vector<std::vector<std::string>>>(value, "rows");
    return legacy_display::Emit(kind, {{"columns", columns}, {"rows", rows}},
                                record);
  }
  if (kind == "image") {
    const auto bytes = Field<std::vector<uint8_t>>(value, "bytes");
    return legacy_display::EmitImage(Field<std::string>(value, "media_type"),
                                     Field<std::string>(value, "alt"), bytes,
                                     record);
  }
  if (kind == "points") {
    const auto x = Field<std::vector<float>>(value, "x");
    const auto y = Field<std::vector<float>>(value, "y");
    const auto z = Field<std::vector<float>>(value, "z");
    const auto colors = Field<std::vector<uint32_t>>(value, "colors");
    return legacy_display::EmitPoints(x, y, z, colors, record);
  }
  if (kind == "line-chart") {
    struct Row {
      std::string id;
      std::string label;
      std::vector<double> x;
      std::vector<double> y;
    };
    std::vector<Row> rows;
    rows.reserve(value.size());
    for (const auto& row : value) {
      rows.push_back({Field<std::string>(row, "id"),
                      row.value("label", std::string{}),
                      Field<std::vector<double>>(row, "x"),
                      Field<std::vector<double>>(row, "y")});
    }
    std::vector<legacy_display::LineSeries> series;
    series.reserve(rows.size());
    for (const auto& row : rows) {
      series.push_back({row.id, row.label, row.x, row.y});
    }
    return legacy_display::EmitLineChart(series, record);
  }
  if (kind == "plotly" || kind == "vega-lite")
    return legacy_display::Emit(kind, value, record);
  if (kind == "module") {
    const auto& members = value.at("blobs");
    std::vector<std::vector<uint8_t>> storage;
    std::vector<card::Blob> blobs;
    storage.reserve(members.size());
    blobs.reserve(members.size());
    for (const auto& [name, bytes] : members.items()) {
      storage.push_back(bytes.get<std::vector<uint8_t>>());
      blobs.push_back({name, storage.back()});
    }
    const auto view = Field<std::string>(value, "view");
    return card::display(card::View{view, blobs}, record);
  }
  return 1;
}

extern "C" auto Display(const char* kind, const char* payload,
                        int record) noexcept -> int {
  try {
    return Emit(kind, Json::parse(payload), record != 0);
  } catch (...) {
    return 1;
  }
}

auto CheckJulia(const char* action) -> void {
  if (auto* exception = jl_exception_occurred()) {
    throw std::runtime_error(std::string(action) + ": " +
                             jl_typeof_str(exception));
  }
}

auto SetEnv(const char* name, const char* value) -> void {
  if (setenv(name, value, 1) != 0)
    throw std::runtime_error(std::string("Cannot set ") + name);
}

// Between calls the worker thread may block in the host while a Julia task
// allocates or sends a display on another Julia thread.
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
    // Julia and Card tasks can still use this library after the caller releases
    // its handle. Keep the library and interpreter alive until process exit.
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(Display), &info) ||
        !dlopen(info.dli_fname, RTLD_NOW | RTLD_LOCAL)) {
      throw std::runtime_error("Could not retain the Julia Card library");
    }
    std::thread([this] { Serve(); }).detach();
  }

  auto Run(const std::string& input) -> Json {
    std::packaged_task<Json()> task([this, &input] {
      if (failure_) std::rethrow_exception(failure_);
      return Json::parse(Call(input));
    });
    auto result = task.get_future();
    {
      // ABI callers are serial, so a single request slot is enough.
      const std::lock_guard lock(mutex_);
      pending_ = std::move(task);
    }
    ready_.notify_one();
    return result.get();
  }

 private:
  auto Serve() -> void {
    // Cache init failures because Julia cannot be initialized again.
    try {
      Prepare();
    } catch (...) {
      failure_ = std::current_exception();
    }
    for (;;) {
      std::packaged_task<Json()> task;
      {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [this] { return pending_.valid(); });
        task = std::move(pending_);
      }
      task();
    }
  }

  auto Call(const std::string& input) const -> std::string {
    const JuliaScope scope;
    jl_value_t* argument = nullptr;
    jl_value_t* value = nullptr;
    JL_GC_PUSH2(&argument, &value);
    argument = jl_pchar_to_string(input.data(), input.size());
    value = jl_call1(run_, argument);
    std::string text;
    try {
      if (value && jl_is_string(value))
        text.assign(jl_string_ptr(value), jl_string_len(value));
    } catch (...) {
      JL_GC_POP();
      throw;
    }
    JL_GC_POP();
    CheckJulia("Running the Julia Card");
    if (text.empty()) throw std::runtime_error("Julia returned no result");
    return text;
  }

  auto Prepare() -> void {
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
    jl_init();
    jl_gc_safe_enter(static_cast<jl_ptls_t>(jl_get_ptls_states()));
    const JuliaScope scope;
    CheckJulia("Initializing Julia");

    // Use addresses for ccall so the bridge remains private to main.so.
    char bridge[256];
    const int length = std::snprintf(
        bridge, sizeof(bridge),
        "const TSUGUMI_JULIA_HOST = (display=Ptr{Cvoid}(%p), "
        "cancelled=Ptr{Cvoid}(%p), remaining_ms=Ptr{Cvoid}(%p))",
        reinterpret_cast<void*>(Display), reinterpret_cast<void*>(IsCancelled),
        reinterpret_cast<void*>(RemainingMs));
    if (length < 0 || static_cast<size_t>(length) >= sizeof(bridge))
      throw std::runtime_error("Could not initialize the Julia bridge");
    jl_eval_string(bridge);
    CheckJulia("Installing the Julia bridge");
    jl_eval_string("Base.include(Main, \"" TSUGUMI_JULIA_SDK "\")");
    CheckJulia("Loading the Julia SDK");
    auto* sdk = reinterpret_cast<jl_module_t*>(
        jl_get_global(jl_main_module, jl_symbol("Tsugumi")));
    auto* load = jl_get_function(sdk, "load");
    run_ = jl_get_function(sdk, "run");

    jl_value_t* root = nullptr;
    jl_value_t* entry = nullptr;
    jl_value_t* prefix = nullptr;
    JL_GC_PUSH3(&root, &entry, &prefix);
    root = jl_cstr_to_string(TSUGUMI_JULIA_SOURCE);
    entry = jl_cstr_to_string(TSUGUMI_JULIA_ENTRY);
    prefix = jl_cstr_to_string(TSUGUMI_JULIA_SOURCE_PREFIX);
    jl_call3(load, root, entry, prefix);
    JL_GC_POP();
    CheckJulia("Loading the Julia Card");
  }

  std::mutex mutex_;
  std::condition_variable ready_;
  std::packaged_task<Json()> pending_;
  jl_value_t* run_ = nullptr;
  std::exception_ptr failure_;
};

}  // namespace

auto RunJulia(const std::string& input) -> Json {
  static auto* const interpreter = new Interpreter;
  return interpreter->Run(input);
}

auto ReadFailure(const Json& failure) -> Tsugumi::Card::CardResultT {
  using Tsugumi::Data::EnumNameFailureKind;
  using Tsugumi::Data::EnumValuesFailureKind;
  using Tsugumi::Data::FailureKind;

  Tsugumi::Card::CardResultT result;
  result.failure = std::make_unique<Tsugumi::Card::CardFailureT>();
  auto& report = *result.failure;
  const auto kind = Field<std::string>(failure, "kind");
  report.kind = FailureKind::worker_failed;
  for (const auto candidate : EnumValuesFailureKind()) {
    if (candidate != FailureKind::unspecified &&
        EnumNameFailureKind(candidate) == kind) {
      report.kind = candidate;
      break;
    }
  }
  report.message = Field<std::string>(failure, "message");
  report.source = failure.value("source", std::string{});
  report.line = failure.value("line", uint32_t{});
  report.cell = failure.value("cell", std::string{});
  return result;
}

}  // namespace tsugumi::julia
