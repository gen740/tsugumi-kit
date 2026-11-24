#include "adapter.hh"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
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

struct ViewEffects {
  tsugumi_effects effects{};
  void* host = nullptr;
};

// A deadline or cancellation query may block while another thread sends.
ViewEffects view_effects;
std::mutex display_mutex;
std::mutex effects_mutex;
const tsugumi_call* active_call = nullptr;

class CallEffects {
 public:
  explicit CallEffects(const tsugumi_call& call) {
    if (call.effects) {
      const std::lock_guard lock(display_mutex);
      view_effects = {*call.effects, call.host};
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

auto GetViewEffects() -> ViewEffects {
  const std::lock_guard lock(display_mutex);
  return view_effects;
}

auto SendDisplay(const tsugumi_view_session& session, tsugumi_bytes bytes)
    -> int {
  constexpr auto kHeader = 5 * sizeof(uint32_t);
  constexpr auto kPayload = TSUGUMI_MESSAGE_MAX - kHeader;
  constexpr uint32_t kMagic = 0x4d475354;
  const auto [effects, host] = GetViewEffects();
  if (!effects.send) return -1;
  const size_t count = bytes.size ? 1 + (bytes.size - 1) / kPayload : 1;
  if (count > std::numeric_limits<uint32_t>::max()) return -1;
  std::vector<uint8_t> chunk(kHeader + std::min(bytes.size, kPayload));
  flatbuffers::WriteScalar(chunk.data(), kMagic);
  // Several processes can send multipart values to the same session.
  if (count > 1) arc4random_buf(chunk.data() + 4, 8);
  flatbuffers::WriteScalar(chunk.data() + 16, static_cast<uint32_t>(count));
  for (size_t index = 0; index < count; ++index) {
    const size_t offset = index * kPayload;
    const size_t size = std::min(bytes.size - offset, kPayload);
    flatbuffers::WriteScalar(chunk.data() + 12, static_cast<uint32_t>(index));
    if (size) std::memcpy(chunk.data() + kHeader, bytes.data + offset, size);
    const int status =
        effects.send(host, &session, chunk.data(), kHeader + size);
    if (status != 0) return status;
  }
  return 0;
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
  if (!input_) Reject(FailureKind::input_invalid, "Invalid CardInput");
  if (!input_->inputs()) return;
  for (const auto* port : *input_->inputs()) {
    const auto name = port->name()->string_view();
    if (!ports_.emplace(name, port).second) {
      Reject(FailureKind::input_invalid,
             "Duplicate input " + std::string(name));
    }
  }
}

auto Input::ReadPort(std::string_view name) -> std::optional<Port> {
  const auto node = ports_.extract(name);
  if (node.empty()) return std::nullopt;
  const auto* port = node.mapped();
  const auto* bytes = port->bytes();
  if (!port->schema() || !bytes)
    Reject(FailureKind::input_invalid,
           "Missing schema or bytes for " + std::string(name));
  return Port{port->schema()->string_view(), {bytes->data(), bytes->size()}};
}

auto CheckSchema(const Input::Port& port, std::string_view schema,
                 std::string_view name) -> void {
  if (port.schema != schema) {
    Reject(FailureKind::input_invalid,
           "Unknown schema for " + std::string(name));
  }
}

auto Input::CheckConsumed() const -> void {
  if (!ports_.empty()) {
    Reject(FailureKind::input_invalid, "Unknown input port");
  }
}

auto AddArtifact(std::vector<ArtifactId>& artifacts, const ArtifactId& id)
    -> void {
  if (std::ranges::none_of(artifacts, [&](const auto& existing) {
        return std::ranges::equal(*existing.bytes(), *id.bytes());
      })) {
    artifacts.push_back(id);
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

namespace card::display {

auto view_open(std::string_view view, uint32_t mode, uint32_t width,
               uint32_t height) -> OpenedView {
  if (view.find('\0') != std::string_view::npos)
    throw std::invalid_argument("View names cannot contain a NUL byte");
  const auto [effects, host] = tsugumi::abi::GetViewEffects();
  OpenedView opened{};
  if (!effects.view_open ||
      effects.view_open(host, std::string(view).c_str(), mode, width, height,
                        &opened.route, &opened.session) != 0) {
    throw std::runtime_error("The host could not open the view");
  }
  return opened;
}

auto route_register(const tsugumi_view_session& session) -> tsugumi_route {
  const auto [effects, host] = tsugumi::abi::GetViewEffects();
  tsugumi_route route{};
  if (!effects.route_register ||
      effects.route_register(host, &session, &route) != 0) {
    throw std::runtime_error("The host could not register the route");
  }
  return route;
}

auto route_unregister(const tsugumi_route& route) -> void {
  const auto [effects, host] = tsugumi::abi::GetViewEffects();
  if (!effects.route_unregister || effects.route_unregister(host, &route) != 0)
    throw std::runtime_error("The host could not unregister the route");
}

auto send_raw(const tsugumi_view_session& session,
              std::span<const uint8_t> bytes) -> int {
  if (bytes.size() > TSUGUMI_MESSAGE_MAX) return -1;
  const auto [effects, host] = tsugumi::abi::GetViewEffects();
  return effects.send ? effects.send(host, &session, bytes.data(), bytes.size())
                      : -1;
}

auto send(const tsugumi_view_session& session, std::span<const uint8_t> bytes)
    -> int {
  return send_raw(session, bytes);
}

auto recv(int64_t timeout_ms) -> Received {
  const auto [effects, host] = tsugumi::abi::GetViewEffects();
  tsugumi_view_event event{};
  const int status = effects.recv ? effects.recv(host, &event, timeout_ms) : -1;
  switch (status) {
    case 0: {
      if (event.kind > 1)
        throw std::runtime_error("The host returned an unknown view event");
      Received result{
          event.kind == 0 ? ReceiveStatus::kMessage : ReceiveStatus::kClose,
          event.route,
          event.session,
          {}};
      if (event.data.size)
        result.bytes.assign(event.data.data, event.data.data + event.data.size);
      return result;
    }
    case 1:
      return {ReceiveStatus::kTimeout, {}, {}, {}};
    case 2:
      return {ReceiveStatus::kCancelled, {}, {}, {}};
    default:
      throw std::runtime_error("The host rejected the view receive");
  }
}

}  // namespace card::display

namespace tsugumi {

auto ArtifactT::dir() -> std::pair<ArtifactT, std::filesystem::path> {
  const std::lock_guard lock(abi::effects_mutex);
  const auto* call = abi::active_call;
  tsugumi_artifact_id id{};
  tsugumi_bytes path{};
  if (!call || !call->effects || !call->effects->artifact_create ||
      call->effects->artifact_create(call->host, &id, &path) != 0)
    throw std::runtime_error("The host could not create an Artifact");
  ArtifactT artifact;
  artifact.id = std::make_unique<ArtifactId>(
      flatbuffers::span<const uint8_t, 16>(id.bytes));
  return {std::move(artifact),
          std::filesystem::path(std::string_view(
              reinterpret_cast<const char*>(path.data), path.size))};
}

auto ArtifactT::upload(const std::filesystem::path& source) -> ArtifactT {
  auto [artifact, path] = dir();
  if (std::filesystem::is_directory(source)) {
    std::filesystem::copy(source, path,
                          std::filesystem::copy_options::recursive);
  } else {
    std::filesystem::copy_file(source, path / source.filename());
  }
  return artifact;
}

auto ArtifactT::open() const -> std::filesystem::path {
  const std::lock_guard lock(abi::effects_mutex);
  const auto* call = abi::active_call;
  tsugumi_artifact_id capability{};
  tsugumi_bytes path{};
  if (id) std::ranges::copy(*id->bytes(), capability.bytes);
  if (!id || !call || !call->effects || !call->effects->artifact_open ||
      call->effects->artifact_open(call->host, &capability, &path) != 0)
    throw std::runtime_error("The host could not open the Artifact");
  return std::filesystem::path(
      std::string_view(reinterpret_cast<const char*>(path.data), path.size));
}

}  // namespace tsugumi
