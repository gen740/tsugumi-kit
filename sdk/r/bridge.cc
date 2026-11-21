#include "bridge.hh"

#include <cstdlib>
#include <memory>

#include "adapter.hh"

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
  const auto& kind = request.at("kind");
  const auto& value = request.at("value");
  if (kind == "text")
    return card::display(FromValue<std::string>(value), record);
  if (kind == "markdown") {
    return card::display(card::Markdown{FromValue<std::string>(value)}, record);
  }
  if (kind == "scalar") {
    return card::display(card::Scalar{FromValue<double>(value.at("value")),
                                      value.at("label").get<std::string>()},
                         record);
  }
  if (kind == "table") {
    const auto columns = Array<std::string>(value.at("columns"));
    const auto rows = ReadVector(value.at("rows"), Array<std::string>);
    return card::display(card::Table{columns, rows}, record);
  }
  if (kind == "image") {
    const auto bytes = FromBase64(value.at("bytes"));
    return card::display(card::Image{value.at("media_type").get<std::string>(),
                                     bytes, value.at("alt").get<std::string>()},
                         record);
  }
  if (kind == "points") {
    const auto x = Array<float>(value.at("x"));
    const auto y = Array<float>(value.at("y"));
    const auto z = Array<float>(value.at("z"));
    const auto colors = Array<uint32_t>(value.at("colors"));
    return card::display(card::Points{x, y, z, colors}, record);
  }
  if (kind == "line-chart") {
    const auto x = ReadVector(
        value, [](const Json& item) { return Array<double>(item.at("x")); });
    const auto y = ReadVector(
        value, [](const Json& item) { return Array<double>(item.at("y")); });
    std::vector<card::Series> views;
    for (size_t i = 0; i < value.size(); ++i) {
      views.push_back({value[i].at("id").get_ref<const std::string&>(),
                       value[i].at("label").get_ref<const std::string&>(), x[i],
                       y[i]});
    }
    return card::display(card::LineChart{views}, record);
  }
  if (kind == "plotly")
    return card::display(card::Plotly{value.dump()}, record);
  if (kind == "vega-lite")
    return card::display(card::VegaLite{value.dump()}, record);
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
