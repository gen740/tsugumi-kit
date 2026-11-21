#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cppcodec/base64_rfc4648.hpp>
#include <limits>
#include <type_traits>

#include "bridge.hh"

namespace tsugumi::julia {

template <class T>
struct Codec;

template <class T>
auto Encode(const T& value) -> Json {
  return Codec<T>::Encode(value);
}

template <class T>
auto Decode(const Json& value) -> T {
  return Codec<T>::Decode(value);
}

inline auto CheckObject(const Json& value,
                        std::initializer_list<std::string_view> fields)
    -> void {
  if (!value.is_object()) throw std::invalid_argument("Expected a JSON object");
  for (const auto& [name, member] : value.items()) {
    if (std::ranges::find(fields, name) == fields.end())
      throw std::invalid_argument("Unknown field " + name);
  }
}

inline auto Required(const Json& value, const char* name) -> const Json& {
  const auto field = value.find(name);
  if (field == value.end() || field->is_null())
    throw std::invalid_argument(std::string("Missing field ") + name);
  return *field;
}

template <std::integral T>
struct Codec<T> {
  static auto Encode(T value) -> Json { return value; }

  static auto Decode(const Json& value) -> T {
    if (value.is_number_unsigned()) {
      const auto number = value.get<uint64_t>();
      if (std::in_range<T>(number)) return static_cast<T>(number);
    } else if (value.is_number_integer()) {
      const auto number = value.get<int64_t>();
      if (std::in_range<T>(number)) return static_cast<T>(number);
    }
    throw std::invalid_argument("Expected an integer in the field's range");
  }
};

template <>
struct Codec<bool> {
  static auto Encode(bool value) -> Json { return value; }
  static auto Decode(const Json& value) -> bool { return value.get<bool>(); }
};

template <std::floating_point T>
struct Codec<T> {
  static auto Encode(T value) -> Json {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value))
      return std::signbit(value) ? "-Infinity" : "Infinity";
    return value;
  }

  static auto Decode(const Json& value) -> T {
    if (value.is_string()) {
      if (value == "NaN") return std::numeric_limits<T>::quiet_NaN();
      if (value == "Infinity") return std::numeric_limits<T>::infinity();
      if (value == "-Infinity") return -std::numeric_limits<T>::infinity();
    }
    const auto number = value.get<double>();
    if (std::abs(number) > std::numeric_limits<T>::max())
      throw std::invalid_argument("Number is out of range for the field");
    return static_cast<T>(number);
  }
};

template <>
struct Codec<std::string> {
  static auto Encode(const std::string& value) -> Json { return value; }
  static auto Decode(const Json& value) -> std::string {
    return value.get<std::string>();
  }
};

template <class T>
struct Codec<std::optional<T>> {
  static auto Encode(const std::optional<T>& value) -> Json {
    return value ? julia::Encode(*value) : Json(nullptr);
  }

  static auto Decode(const Json& value) -> std::optional<T> {
    return value.is_null() ? std::nullopt
                           : std::optional(julia::Decode<T>(value));
  }
};

template <class T>
struct Codec<std::unique_ptr<T>> {
  static auto Encode(const std::unique_ptr<T>& value) -> Json {
    return value ? julia::Encode(*value) : Json(nullptr);
  }

  static auto Decode(const Json& value) -> std::unique_ptr<T> {
    return value.is_null() ? nullptr
                           : std::make_unique<T>(julia::Decode<T>(value));
  }
};

template <class T>
struct Codec<std::vector<T>> {
  static auto Encode(const std::vector<T>& value) -> Json {
    auto result = Json::array();
    for (const auto& member : value) result.push_back(Codec<T>::Encode(member));
    return result;
  }

  static auto Decode(const Json& value) -> std::vector<T> {
    if (!value.is_array()) throw std::invalid_argument("Expected an array");
    std::vector<T> result;
    result.reserve(value.size());
    for (const auto& member : value) result.push_back(julia::Decode<T>(member));
    return result;
  }
};

using Base64 = cppcodec::base64_rfc4648;

template <class T, class U, uint16_t size>
auto EncodeArray(const flatbuffers::Array<U, size>& array) -> Json {
  auto result = Json::array();
  for (uint16_t index = 0; index < size; ++index) {
    const auto value = array.Get(index);
    if constexpr (std::is_pointer_v<decltype(value)>)
      result.push_back(Encode(*value));
    else
      result.push_back(Encode(static_cast<T>(value)));
  }
  return result;
}

template <class T, class U, size_t size>
auto DecodeArray(const Json& value) -> std::array<U, size> {
  if (!value.is_array() || value.size() != size)
    throw std::invalid_argument("Expected a fixed-length array");
  std::array<U, size> result;
  for (size_t index = 0; index < size; ++index)
    result[index] = static_cast<U>(Decode<T>(value[index]));
  return result;
}

}  // namespace tsugumi::julia
