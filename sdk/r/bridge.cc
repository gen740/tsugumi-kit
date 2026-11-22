#include "bridge.hh"

#include <cstdlib>
#include <memory>

#include "adapter.hh"
#include "legacy-display.hh"

extern "C" auto tsugumi_r_run(const char* input) -> char*;

namespace tsugumi::r {

auto RunR(const Json& input) -> Json {
  const std::unique_ptr<char, decltype(&std::free)> reply(
      tsugumi_r_run(input.dump().c_str()), std::free);
  if (!reply)
    abi9::Reject(abi9::FailureKind::worker_failed,
                 "Embedded R could not initialize or complete the call");
  return Json::parse(reply.get());
}

namespace {

template <typename T>
auto Array(const Json& value) -> std::vector<T> {
  return ReadVector(value, FromValue<T>);
}

auto Display(const Json& request, bool record) -> int {
  const auto kind = request.at("kind").get<std::string>();
  const auto& value = request.at("value");
  if (kind == "text" || kind == "markdown")
    return legacy_display::Emit(kind, {{"text", FromValue<std::string>(value)}},
                                record);
  if (kind == "table") {
    const auto columns = Array<std::string>(value.at("columns"));
    const auto rows = ReadVector(value.at("rows"), Array<std::string>);
    return legacy_display::Emit(kind, {{"columns", columns}, {"rows", rows}},
                                record);
  }
  if (kind == "image") {
    const auto bytes = FromBase64(value.at("bytes"));
    return legacy_display::EmitImage(value.at("media_type").get<std::string>(),
                                     value.at("alt").get<std::string>(), bytes,
                                     record);
  }
  if (kind == "points") {
    const auto x = Array<float>(value.at("x"));
    const auto y = Array<float>(value.at("y"));
    const auto z = Array<float>(value.at("z"));
    const auto colors = Array<uint32_t>(value.at("colors"));
    return legacy_display::EmitPoints(x, y, z, colors, record);
  }
  if (kind == "line-chart") {
    const auto x = ReadVector(
        value, [](const Json& item) { return Array<double>(item.at("x")); });
    const auto y = ReadVector(
        value, [](const Json& item) { return Array<double>(item.at("y")); });
    std::vector<legacy_display::LineSeries> views;
    for (size_t i = 0; i < value.size(); ++i) {
      views.push_back({value[i].at("id").get_ref<const std::string&>(),
                       value[i].at("label").get_ref<const std::string&>(), x[i],
                       y[i]});
    }
    return legacy_display::EmitLineChart(views, record);
  }
  if (kind == "plotly" || kind == "vega-lite")
    return legacy_display::Emit(kind, value, record);
  if (kind == "module") {
    const auto& blobs = value.at("blobs");
    if (!blobs.is_object()) throw std::invalid_argument("Expected named blobs");
    std::vector<std::vector<uint8_t>> storage;
    std::vector<card::Blob> views;
    for (const auto& [name, bytes] : blobs.items()) {
      storage.push_back(FromBase64(bytes));
      views.push_back({name, storage.back()});
    }
    return card::display(card::Module{value.at("module").get<std::string>(),
                                      value.at("config").dump(), views},
                         record);
  }
  throw std::invalid_argument("Unknown display type");
}

}  // namespace
}  // namespace tsugumi::r

extern "C" auto tsugumi_r_cancelled() noexcept -> bool {
  try {
    return card::cancelled();
  } catch (...) {
    return true;
  }
}

extern "C" auto tsugumi_r_remaining_ms() noexcept -> double {
  try {
    return static_cast<double>(card::remaining_ms());
  } catch (...) {
    return 0;
  }
}

extern "C" auto tsugumi_r_display(const char* request, bool record) noexcept
    -> bool {
  try {
    return tsugumi::r::Display(tsugumi::r::Json::parse(request), record) == 0;
  } catch (...) {
    return false;
  }
}

extern "C" auto tsugumi_r_send_display(tsugumi_bytes bytes, int record) noexcept
    -> int {
  try {
    return tsugumi::abi9::SendDisplay(bytes, record != 0);
  } catch (...) {
    return 1;
  }
}
