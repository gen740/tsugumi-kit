#include "adapter.hh"

#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace tsugumi::abi {
namespace {

using Tsugumi::Card::CardFailureT;
using Tsugumi::Card::CardResult;
using Tsugumi::Card::CardResultT;

// Kept until the next call. The worker invokes one call at a time.
flatbuffers::DetachedBuffer result_buffer;

struct DisplayEffect {
  decltype(tsugumi_effects::send) callback = nullptr;
  void* host = nullptr;
};

// A deadline or cancellation query may block while another thread sends.
DisplayEffect display_effect;
std::mutex display_mutex;
std::mutex effects_mutex;
const tsugumi_call* active_call = nullptr;

class CallEffects {
 public:
  explicit CallEffects(const tsugumi_call& call) {
    if (call.effects && call.effects->send) {
      const std::lock_guard lock(display_mutex);
      display_effect = {call.effects->send, call.host};
    }
    const std::lock_guard lock(effects_mutex);
    active_call = &call;
  }

  ~CallEffects() {
    const std::lock_guard lock(effects_mutex);
    active_call = nullptr;
  }

  CallEffects(const CallEffects&) = delete;
  auto operator=(const CallEffects&) -> CallEffects& = delete;
};

auto FailureResult(FailureKind kind, const char* message) -> CardResultT {
  CardResultT result;
  result.failure = std::make_unique<CardFailureT>();
  result.failure->kind = kind;
  result.failure->message = message;
  return result;
}

auto Run(CardBody body, tsugumi_bytes input) -> CardResultT {
  try {
    return body(input);
  } catch (const Failure& error) {
    const bool valid_kind = error.kind != FailureKind::unspecified &&
                            *Tsugumi::Data::EnumNameFailureKind(error.kind);
    auto result = FailureResult(
        valid_kind ? error.kind : FailureKind::worker_failed, error.what());
    if (!valid_kind) {
      result.failure->message =
          "Invalid FailureKind " +
          std::to_string(static_cast<uint16_t>(error.kind)) + ": " +
          result.failure->message;
    }
    const std::filesystem::path path(error.location.file_name());
    const auto relative = path.lexically_normal();
    if (!relative.empty() && relative.is_relative() && relative != "." &&
        *relative.begin() != "..") {
      result.failure->source = relative.generic_string();
      result.failure->line = error.location.line();
    }
    result.failure->cell = error.cell;
    return result;
  } catch (const std::exception& error) {
    return FailureResult(FailureKind::worker_failed, error.what());
  } catch (...) {
    return FailureResult(FailureKind::worker_failed,
                         "Card threw a non-standard exception");
  }
}

}  // namespace

auto SendDisplay(const char* view, tsugumi_bytes bytes) -> int {
  DisplayEffect effect;
  {
    const std::lock_guard lock(display_mutex);
    effect = display_effect;
  }
  if (!effect.callback) return 1;
  try {
    return effect.callback(effect.host, view, bytes.data, bytes.size);
  } catch (...) {
    return 1;
  }
}

auto Invoke(const tsugumi_call* call, tsugumi_bytes* result,
            CardBody body) noexcept -> int {
  // A different ABI may provide only the leading version at this address.
  if (!call || !result) return 1;
  uint32_t version;
  std::memcpy(&version, call, sizeof(version));
  if (version != TSUGUMI_ABI_VERSION) return 1;
  *result = {};
  result_buffer = {};
  try {
    const CallEffects effects(*call);
    const auto value = Run(body, call->input);
    flatbuffers::FlatBufferBuilder builder;
    builder.Finish(CardResult::Pack(builder, &value));
    result_buffer = builder.Release();
    *result = {result_buffer.data(), result_buffer.size()};
    return 0;
  } catch (...) {
    return 1;
  }
}

Buffer::Buffer(tsugumi_bytes bytes) : size_(bytes.size) {
  if (!bytes.data || size_ < FLATBUFFERS_MIN_BUFFER_SIZE ||
      size_ >= FLATBUFFERS_MAX_BUFFER_SIZE) {
    return;
  }
  data_ = bytes.data;
  if (reinterpret_cast<uintptr_t>(data_) % alignof(Block) != 0) {
    aligned_.resize((size_ + sizeof(Block) - 1) / sizeof(Block));
    std::memcpy(aligned_.data(), data_, size_);
    data_ = reinterpret_cast<const uint8_t*>(aligned_.data());
  }
}

auto Reject(FailureKind kind, const std::string& message) -> void {
  throw Failure(kind, message, {});
}

Input::Input(tsugumi_bytes bytes)
    : buffer_(bytes), input_(buffer_.Verify<Tsugumi::Card::CardInput>()) {
  if (!input_) Reject(FailureKind::invalid_request, "Invalid CardInput");
  if (!input_->inputs()) return;
  for (const auto* port : *input_->inputs()) {
    switch (port->kind()) {
      case PortKind::FlatBuffer:
        if (port->path())
          Reject(FailureKind::invalid_request, "FlatBuffer input has a path");
        break;
      case PortKind::Path: {
        const auto* path = port->path();
        if (!path || path->string_view().find('\0') != std::string_view::npos ||
            !std::filesystem::path(path->string_view()).is_absolute() ||
            (port->bytes() && port->bytes()->size() != 0)) {
          Reject(FailureKind::invalid_request,
                 "Invalid Path input " + port->name()->str());
        }
        break;
      }
      default:
        Reject(FailureKind::invalid_request, "Unknown input port kind");
    }
    const auto name = port->name()->string_view();
    if (!ports_.emplace(name, port).second) {
      Reject(FailureKind::invalid_request,
             "Duplicate input " + std::string(name));
    }
  }
}

auto Input::TakePort(std::string_view name, PortKind kind)
    -> const Tsugumi::Card::CardPort* {
  const auto port = ports_.extract(name);
  if (port.empty()) return nullptr;
  if (port.mapped()->kind() != kind) {
    Reject(FailureKind::invalid_request,
           "Wrong kind for input " + std::string(name));
  }
  return port.mapped();
}

auto Input::ReadPath(std::string_view name)
    -> std::optional<std::filesystem::path> {
  const auto* port = TakePort(name, PortKind::Path);
  return port
             ? std::optional{std::filesystem::path(port->path()->string_view())}
             : std::nullopt;
}

auto Input::ReadPortBytes(std::string_view name) -> std::optional<tsugumi_bytes> {
  const auto* port = TakePort(name, PortKind::FlatBuffer);
  if (!port) return std::nullopt;
  const auto* bytes = port->bytes();
  return bytes ? tsugumi_bytes{bytes->data(), bytes->size()} : tsugumi_bytes{};
}

auto WritePath(Tsugumi::Card::CardResultT& result, std::string_view name,
               const std::optional<std::filesystem::path>& path, bool required)
    -> void {
  if (!path) {
    if (required)
      Reject(FailureKind::output_invalid,
             "Missing output " + std::string(name));
    return;
  }
  auto port = std::make_unique<Tsugumi::Card::CardPortT>();
  port->name = name;
  port->kind = PortKind::Path;
  port->path = path->native();
  if (port->path.empty() || port->path.find('\0') != std::string::npos)
    Reject(FailureKind::output_invalid,
           "Invalid Path output " + std::string(name));
  result.outputs.push_back(std::move(port));
}

auto Input::Params() const -> tsugumi_bytes {
  const auto* params = input_->params();
  return params ? tsugumi_bytes{params->data(), params->size()}
                : tsugumi_bytes{};
}

auto Input::CheckConsumed() const -> void {
  if (!ports_.empty()) {
    Reject(FailureKind::invalid_request, "Unknown input port");
  }
}

}  // namespace tsugumi::abi

namespace card {

using tsugumi::abi::active_call;
using tsugumi::abi::effects_mutex;

auto cancelled() -> bool {
  const std::lock_guard lock(effects_mutex);
  return !active_call || !active_call->effects ||
         !active_call->effects->cancelled ||
         active_call->effects->cancelled(active_call->host) != 0;
}

auto remaining_ms() -> int64_t {
  const std::lock_guard lock(effects_mutex);
  return active_call && active_call->effects &&
                 active_call->effects->remaining_ms
             ? active_call->effects->remaining_ms(active_call->host)
             : 0;
}

}  // namespace card
