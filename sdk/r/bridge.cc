#include "bridge.hh"

#include <cstdlib>
#include <memory>

#include "adapter.hh"
#include "display.hh"

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

auto Display(const Json& request) -> int {
  const auto kind = request.at("kind").get<std::string>();
  const auto& value = request.at("value");
  if (kind == "text" || kind == "markdown")
    return display::Emit(kind, {{"text", FromValue<std::string>(value)}});
  if (kind == "table") {
    const auto columns = Array<std::string>(value.at("columns"));
    const auto rows = ReadVector(value.at("rows"), Array<std::string>);
    return display::Emit(kind, {{"columns", columns}, {"rows", rows}});
  }
  if (kind == "image") {
    const auto bytes = FromBase64(value.at("bytes"));
    return card::display::send(
        card::display::Image{value.at("media_type").get<std::string>(), bytes,
                             value.at("alt").get<std::string>()});
  }
  if (kind == "points") {
    const auto x = Array<float>(value.at("x"));
    const auto y = Array<float>(value.at("y"));
    const auto z = Array<float>(value.at("z"));
    const auto colors = Array<uint32_t>(value.at("colors"));
    return card::display::send(card::display::Points{x, y, z, colors});
  }
  if (kind == "line-chart") {
    const auto x = ReadVector(
        value, [](const Json& item) { return Array<double>(item.at("x")); });
    const auto y = ReadVector(
        value, [](const Json& item) { return Array<double>(item.at("y")); });
    std::vector<card::display::Series> views;
    for (size_t i = 0; i < value.size(); ++i) {
      views.push_back({value[i].at("id").get_ref<const std::string&>(),
                       value[i].at("label").get_ref<const std::string&>(), x[i],
                       y[i]});
    }
    return card::display::send(card::display::LineChart{views});
  }
  if (kind == "plotly" || kind == "vega-lite")
    return display::Emit(kind, value);
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

extern "C" auto tsugumi_r_display(const char* request, int* status) noexcept
    -> bool {
  try {
    *status = tsugumi::r::Display(tsugumi::r::Json::parse(request));
    return true;
  } catch (...) {
    return false;
  }
}

extern "C" auto tsugumi_r_send(const char* view, const uint8_t* bytes,
                               size_t size) noexcept -> int {
  try {
    return tsugumi::abi9::SendDisplay(view, {bytes, size});
  } catch (...) {
    return 1;
  }
}
