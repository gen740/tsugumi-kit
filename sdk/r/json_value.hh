#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace tsugumi::r {

using Json = nlohmann::json;

inline constexpr uint64_t kExactInteger = UINT64_C(1) << 53;
inline constexpr std::string_view kBase64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline auto Member(const Json& object, const char* name) -> const Json& {
  static const Json missing;
  if (!object.is_object()) throw std::invalid_argument("Expected an object");
  const auto field = object.find(name);
  return field == object.end() ? missing : *field;
}

template <typename T>
auto ToValue(T value) -> Json {
  if constexpr (std::is_floating_point_v<T>) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-Inf" : "Inf";
  } else if constexpr (std::is_integral_v<T> && sizeof(T) == 8) {
    if (value > static_cast<T>(kExactInteger) ||
        (std::is_signed_v<T> &&
         value < static_cast<T>(-int64_t{kExactInteger})))
      return std::to_string(value);
  }
  return value;
}

template <typename T>
auto ReadInteger(const Json& value) -> T {
  T result{};
  if (value.is_string()) {
    const auto& text = value.get_ref<const std::string&>();
    const auto* end = text.data() + text.size();
    const auto [last, error] = std::from_chars(text.data(), end, result);
    if (error == std::errc{} && last == end) return result;
  } else if (value.is_number()) {
    // JSON numbers from R must lie within its exact integer range.
    const auto number = value.get<double>();
    if (std::abs(number) <= kExactInteger && std::trunc(number) == number) {
      const auto integer = static_cast<int64_t>(number);
      if (std::in_range<T>(integer)) return static_cast<T>(integer);
    }
  }
  throw std::invalid_argument("Expected an in-range integer or decimal string");
}

template <typename T>
auto FromValue(const Json& value) -> T {
  if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string>) {
    return value.get<T>();
  } else if constexpr (std::is_integral_v<T>) {
    return ReadInteger<T>(value);
  } else {
    static_assert(std::is_floating_point_v<T>);
    if (value.is_number()) {
      const auto number = value.get<double>();
      if (std::abs(number) <= std::numeric_limits<T>::max())
        return static_cast<T>(number);
    } else if (value == "NaN") {
      return std::numeric_limits<T>::quiet_NaN();
    } else if (value == "Inf") {
      return std::numeric_limits<T>::infinity();
    } else if (value == "-Inf") {
      return -std::numeric_limits<T>::infinity();
    }
    throw std::invalid_argument("Expected a number or NaN/Inf token");
  }
}

template <bool Numeric = false, typename Range, typename Convert>
auto ToArray(const Range& values, Convert convert) -> Json {
  Json array = Json::array();
  for (const auto& value : values) array.push_back(convert(value));
  // FlatBuffers field names cannot start with '$'. Keep nested tables and
  // lists distinct from numeric vectors, even when the vector is empty.
  if constexpr (Numeric) return Json{{"$numeric", std::move(array)}};
  return array;
}

template <bool AllowScalar = false, typename Convert>
auto ReadVector(const Json& array, Convert convert)
    -> std::vector<decltype(convert(array[0]))> {
  if constexpr (AllowScalar) {
    if (array.is_primitive()) return {convert(array)};
  }
  if (!array.is_array()) throw std::invalid_argument("Expected an array");
  std::vector<decltype(convert(array[0]))> values;
  values.reserve(array.size());
  for (const auto& item : array) values.push_back(convert(item));
  return values;
}

template <typename T, size_t N, bool AllowScalar = false, typename Convert>
auto ReadArray(const Json& array, Convert convert) -> std::array<T, N> {
  if constexpr (N == 1 && AllowScalar) {
    if (array.is_primitive()) return {convert(array)};
  }
  if (!array.is_array() || array.size() != N)
    throw std::invalid_argument("Wrong size for fixed array");
  std::array<T, N> values;
  for (size_t i = 0; i < N; ++i) values[i] = convert(array[i]);
  return values;
}

inline auto ToBase64(const std::vector<uint8_t>& bytes) -> Json {
  std::string result;
  result.reserve((bytes.size() + 2) / 3 * 4);
  for (size_t i = 0; i < bytes.size(); i += 3) {
    const auto remaining = bytes.size() - i;
    const uint32_t triple = (uint32_t{bytes[i]} << 16) |
                            (remaining >= 2 ? uint32_t{bytes[i + 1]} << 8 : 0) |
                            (remaining >= 3 ? bytes[i + 2] : 0);
    result += kBase64[(triple >> 18) & 63];
    result += kBase64[(triple >> 12) & 63];
    result += remaining >= 2 ? kBase64[(triple >> 6) & 63] : '=';
    result += remaining >= 3 ? kBase64[triple & 63] : '=';
  }
  return result;
}

inline auto FromBase64(const Json& value) -> std::vector<uint8_t> {
  std::string_view text = value.get_ref<const std::string&>();
  std::string unwrapped;
  // jsonlite wraps base64 from raw values, including image and module blobs.
  if (text.find_first_of("\r\n") != text.npos) {
    unwrapped.reserve(text.size());
    for (const char ch : text) {
      if (ch != '\r' && ch != '\n') unwrapped.push_back(ch);
    }
    text = unwrapped;
  }
  const auto invalid = [] { throw std::invalid_argument("Invalid base64"); };
  if (text.size() % 4 != 0) invalid();
  std::vector<uint8_t> bytes;
  bytes.reserve(text.size() / 4 * 3);
  for (size_t i = 0; i < text.size(); i += 4) {
    const auto a = kBase64.find(text[i]);
    const auto b = kBase64.find(text[i + 1]);
    const bool one = text[i + 2] == '=';
    const bool two = text[i + 3] == '=';
    const auto c = one ? 0 : kBase64.find(text[i + 2]);
    const auto d = two ? 0 : kBase64.find(text[i + 3]);
    if (a == kBase64.npos || b == kBase64.npos || c == kBase64.npos ||
        d == kBase64.npos || ((one || two) && i + 4 != text.size()) ||
        (one && (!two || (b & 15))) || (two && (c & 3)))
      invalid();
    bytes.push_back(static_cast<uint8_t>((a << 2) | (b >> 4)));
    if (!one) bytes.push_back(static_cast<uint8_t>((b << 4) | (c >> 2)));
    if (!two) bytes.push_back(static_cast<uint8_t>((c << 6) | d));
  }
  return bytes;
}

}  // namespace tsugumi::r
