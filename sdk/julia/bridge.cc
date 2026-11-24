#include "bridge.hh"

#include <dlfcn.h>
#include <julia.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

#include "display.hh"

extern "C" auto tsugumi_julia_artifact_dir(int keep, uint8_t* id) -> char* {
  try {
    const auto [artifact, path] = card::Artifact::dir(keep != 0);
    std::memcpy(id, artifact.id->bytes()->Data(), 16);
    return strdup(path.c_str());
  } catch (...) {
    return nullptr;
  }
}

extern "C" auto tsugumi_julia_artifact_upload(const char* path, int keep,
                                              uint8_t* id) -> int {
  try {
    const auto artifact = card::Artifact::upload(path, keep != 0);
    std::memcpy(id, artifact.id->bytes()->Data(), 16);
    return 0;
  } catch (...) {
    return -1;
  }
}

// Sets *path to the directory, or on failure to the message or nullptr.
extern "C" auto tsugumi_julia_artifact_open(const uint8_t* id, char** path)
    -> int {
  try {
    card::Artifact artifact;
    artifact.id = std::make_unique<tsugumi::ArtifactId>(
        flatbuffers::span<const uint8_t, 16>(id, 16));
    *path = strdup(artifact.open().c_str());
    return *path ? 0 : -1;
  } catch (const std::exception& failure) {
    *path = strdup(failure.what());
    return -1;
  } catch (...) {
    *path = nullptr;
    return -1;
  }
}

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

auto Emit(std::string_view kind, const Json& value) -> int {
  if (kind == "text" || kind == "markdown")
    return display::Emit(kind, {{"text", value.get<std::string>()}});
  if (kind == "table") {
    const auto columns = Field<std::vector<std::string>>(value, "columns");
    const auto rows =
        Field<std::vector<std::vector<std::string>>>(value, "rows");
    return display::Emit(kind, {{"columns", columns}, {"rows", rows}});
  }
  if (kind == "image") {
    const auto bytes = Field<std::vector<uint8_t>>(value, "bytes");
    return card::display::send(
        card::display::Image{Field<std::string>(value, "media_type"), bytes,
                             Field<std::string>(value, "alt")});
  }
  if (kind == "points") {
    const auto x = Field<std::vector<float>>(value, "x");
    const auto y = Field<std::vector<float>>(value, "y");
    const auto z = Field<std::vector<float>>(value, "z");
    const auto colors = Field<std::vector<uint32_t>>(value, "colors");
    return card::display::send(card::display::Points{x, y, z, colors});
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
    std::vector<card::display::Series> series;
    series.reserve(rows.size());
    for (const auto& row : rows) {
      series.push_back({row.id, row.label, row.x, row.y});
    }
    return card::display::send(card::display::LineChart{series});
  }
  if (kind == "plotly" || kind == "vega-lite")
    return display::Emit(kind, value);
  throw std::invalid_argument("Unknown display type");
}

extern "C" auto EmitDisplay(const char* kind, const char* payload,
                            int* status) noexcept -> int {
  try {
    *status = Emit(kind, Json::parse(payload));
    return 0;
  } catch (...) {
    return 1;
  }
}

extern "C" auto OpenView(const char* view, uint32_t mode, uint32_t flags,
                         uint32_t width, uint32_t height, tsugumi_route* route,
                         tsugumi_view_session* session) noexcept -> int {
  try {
    const auto opened =
        card::display::view_open(view, mode, flags, width, height);
    *route = opened.route;
    *session = opened.session;
    return 0;
  } catch (...) {
    return -1;
  }
}

extern "C" auto RegisterRoute(const tsugumi_view_session* session,
                              tsugumi_route* route) noexcept -> int {
  try {
    *route = card::display::route_register(*session);
    return 0;
  } catch (...) {
    return -1;
  }
}

extern "C" auto UnregisterRoute(const tsugumi_route* route) noexcept -> int {
  try {
    card::display::route_unregister(*route);
    return 0;
  } catch (...) {
    return -1;
  }
}

extern "C" auto Send(const tsugumi_view_session* session, const uint8_t* bytes,
                     size_t size) noexcept -> int {
  try {
    return card::display::send(*session, {bytes, size});
  } catch (...) {
    return -1;
  }
}

extern "C" auto Receive(int64_t timeout_ms, tsugumi_view_event* event) noexcept
    -> int {
  try {
    const auto received = card::display::recv(timeout_ms);
    using card::display::ReceiveStatus;
    if (received.status == ReceiveStatus::kTimeout) return 1;
    if (received.status == ReceiveStatus::kCancelled) return 2;
    uint8_t* data = nullptr;
    if (!received.bytes.empty()) {
      data = static_cast<uint8_t*>(std::malloc(received.bytes.size()));
      if (!data) return -1;
      std::memcpy(data, received.bytes.data(), received.bytes.size());
    }
    *event = {received.status == ReceiveStatus::kClose ? 1u : 0u,
              received.route,
              received.session,
              {data, received.bytes.size()}};
    return 0;
  } catch (...) {
    return -1;
  }
}

auto CheckJulia(const char* action) -> void {
  if (auto* exception = jl_exception_occurred()) {
    throw std::runtime_error(std::string(action) + ": " +
                             jl_typeof_str(exception));
  }
}

extern "C" auto tsugumi_julia_on_owner(void (*callback)(void*), void* context)
    -> void;

class CardModule {
 public:
  CardModule() {
    // Card tasks can call the bridge after the host releases its handle.
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(EmitDisplay), &info) ||
        !dlopen(info.dli_fname, RTLD_NOW | RTLD_LOCAL)) {
      throw std::runtime_error("Could not retain the Julia Card library");
    }
  }

  auto Run(const std::string& input) -> Json {
    struct CallContext {
      CardModule& card;
      const std::string& input;
      std::string reply;
    } context{*this, input, {}};
    tsugumi_julia_on_owner(
        [](void* opaque) {
          auto& call = *static_cast<CallContext*>(opaque);
          call.reply = call.card.Call(call.input);
        },
        &context);
    return Json::parse(context.reply);
  }

 private:
  auto Call(const std::string& input) -> std::string {
    std::call_once(load_once_, [this] {
      try {
        Prepare();
      } catch (...) {
        load_error_ = std::current_exception();
      }
    });
    if (load_error_) std::rethrow_exception(load_error_);
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
    char name[64];
    std::snprintf(name, sizeof(name), "TsugumiCard_%p",
                  static_cast<void*>(this));
    const std::string module = name;
    jl_eval_string(("module " + module + " end").c_str());
    CheckJulia("Creating the Julia Card namespace");

    const auto eval = [&](std::string code, const char* action) {
      jl_eval_string(("Core.eval(" + module + ", :(" + code + "))").c_str());
      CheckJulia(action);
    };

    // Use addresses for ccall so the bridge remains private to main.so.
    char bridge[768];
    const int length = std::snprintf(
        bridge, sizeof(bridge),
        "const TSUGUMI_JULIA_HOST = (emit=Ptr{Cvoid}(%p), send=Ptr{Cvoid}(%p), "
        "cancelled=Ptr{Cvoid}(%p), remaining_ms=Ptr{Cvoid}(%p), "
        "recv=Ptr{Cvoid}(%p), dir=Ptr{Cvoid}(%p), upload=Ptr{Cvoid}(%p), "
        "open=Ptr{Cvoid}(%p), view_open=Ptr{Cvoid}(%p), "
        "route_register=Ptr{Cvoid}(%p), route_unregister=Ptr{Cvoid}(%p))",
        reinterpret_cast<void*>(EmitDisplay), reinterpret_cast<void*>(Send),
        reinterpret_cast<void*>(IsCancelled),
        reinterpret_cast<void*>(RemainingMs), reinterpret_cast<void*>(Receive),
        reinterpret_cast<void*>(tsugumi_julia_artifact_dir),
        reinterpret_cast<void*>(tsugumi_julia_artifact_upload),
        reinterpret_cast<void*>(tsugumi_julia_artifact_open),
        reinterpret_cast<void*>(OpenView),
        reinterpret_cast<void*>(RegisterRoute),
        reinterpret_cast<void*>(UnregisterRoute));
    if (length < 0 || static_cast<size_t>(length) >= sizeof(bridge))
      throw std::runtime_error("Could not initialize the Julia bridge");
    eval(bridge, "Installing the Julia bridge");
    eval("const TSUGUMI_JULIA_CARD_API = \"" TSUGUMI_JULIA_CARD_API "\"",
         "Locating the Julia Card API");
    eval("const TSUGUMI_JULIA_PARAMS = \"" TSUGUMI_JULIA_PARAMS "\"",
         "Locating the Julia parameters");
    jl_eval_string(
        ("Base.include(" + module + ", \"" TSUGUMI_JULIA_SDK "\")").c_str());
    CheckJulia("Loading the Julia SDK");
    auto* card = reinterpret_cast<jl_module_t*>(
        jl_get_global(jl_main_module, jl_symbol(name)));
    auto* sdk = reinterpret_cast<jl_module_t*>(
        jl_get_global(card, jl_symbol("Tsugumi")));
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

  std::once_flag load_once_;
  jl_value_t* run_ = nullptr;
  std::exception_ptr load_error_;
};

}  // namespace

auto RunJulia(const std::string& input) -> Json {
  static auto* const card = new CardModule;
  return card->Run(input);
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
