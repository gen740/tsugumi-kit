#pragma once

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace tsugumi::ruby {

using Json = nlohmann::json;

auto CheckObject(const Json& value,
                 std::initializer_list<std::string_view> fields) -> void;
auto EncodeBytes(const std::vector<uint8_t>& bytes) -> std::string;
auto DecodeBytes(std::string_view encoded) -> std::vector<uint8_t>;

template <class T>
struct Codec {
  static auto Encode(T value) -> Json {
    if constexpr (std::is_floating_point_v<T>) {
      if (std::isnan(value)) return "NaN";
      if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
    }
    return value;
  }

  static auto Decode(const Json& value) -> T {
    if constexpr (std::is_same_v<T, bool>) {
      return value.get<bool>();
    } else if constexpr (std::is_integral_v<T>) {
      if (value.is_number_unsigned()) {
        const auto integer = value.get<uint64_t>();
        if (integer <= static_cast<uint64_t>(std::numeric_limits<T>::max()))
          return static_cast<T>(integer);
      } else if (value.is_number_integer()) {
        const auto integer = value.get<int64_t>();
        if (integer >= 0) return Decode(Json(static_cast<uint64_t>(integer)));
        if constexpr (std::is_signed_v<T>) {
          if (integer >= std::numeric_limits<T>::min())
            return static_cast<T>(integer);
        }
      }
      throw std::invalid_argument(
          "integer is out of range or has the wrong type");
    } else if constexpr (std::is_floating_point_v<T>) {
      if (value == "NaN") return std::numeric_limits<T>::quiet_NaN();
      if (value == "Infinity") return std::numeric_limits<T>::infinity();
      if (value == "-Infinity") return -std::numeric_limits<T>::infinity();
      if (value.is_number()) {
        const auto number = value.get<double>();
        if (std::abs(number) <= std::numeric_limits<T>::max())
          return static_cast<T>(number);
      }
      throw std::invalid_argument(
          "float is out of range or has the wrong type");
    } else {
      static_assert(std::is_same_v<T, std::string>);
      return value.get<std::string>();
    }
  }
};

template <class T>
struct Codec<std::unique_ptr<T>> {
  static auto Encode(const std::unique_ptr<T>& value) -> Json {
    return value ? Codec<T>::Encode(*value) : Json(nullptr);
  }

  static auto Decode(const Json& value) -> std::unique_ptr<T> {
    return value.is_null() ? nullptr
                           : std::make_unique<T>(Codec<T>::Decode(value));
  }
};

template <class T>
struct Codec<std::optional<T>> {
  static auto Encode(const std::optional<T>& value) -> Json {
    return value ? Codec<T>::Encode(*value) : Json(nullptr);
  }

  static auto Decode(const Json& value) -> std::optional<T> {
    if (value.is_null()) return std::nullopt;
    return Codec<T>::Decode(value);
  }
};

template <class T>
struct Codec<std::vector<T>> {
  static auto Encode(const std::vector<T>& values) -> Json {
    Json result = Json::array();
    for (const auto& value : values) result.push_back(Codec<T>::Encode(value));
    return result;
  }

  static auto Decode(const Json& values) -> std::vector<T> {
    if (!values.is_array()) throw std::invalid_argument("expected an array");
    std::vector<T> result;
    result.reserve(values.size());
    for (const auto& value : values) {
      auto item = Codec<T>::Decode(value);
      // A FlatBuffers vector of tables has no null elements. The object API
      // dereferences these pointers when packing, before verification.
      if constexpr (requires { item.get(); }) {
        if (!item) throw std::invalid_argument("unexpected null array element");
      }
      result.push_back(std::move(item));
    }
    return result;
  }
};

template <>
struct Codec<std::vector<uint8_t>> {
  static auto Encode(const std::vector<uint8_t>& values) -> Json {
    return EncodeBytes(values);
  }

  static auto Decode(const Json& value) -> std::vector<uint8_t> {
    return DecodeBytes(value.get_ref<const std::string&>());
  }
};

template <class T>
auto Encode(const T& value) -> Json {
  return Codec<T>::Encode(value);
}

template <class T>
auto ReadMember(const Json& object, const char* name, T& value, bool required)
    -> void {
  const auto field = object.find(name);
  if (field == object.end()) {
    if (required)
      throw std::invalid_argument(std::string("missing field ") + name);
    return;
  }
  if (required && field->is_null())
    throw std::invalid_argument(std::string("null field ") + name);
  value = Codec<T>::Decode(*field);
}

template <class T, class Storage, uint16_t N>
auto EncodeArray(const flatbuffers::Array<Storage, N>& values) -> Json {
  Json result = Json::array();
  for (uint16_t index = 0; index < N; ++index) {
    if constexpr (std::is_pointer_v<decltype(values.Get(index))>) {
      result.push_back(Encode(*values.Get(index)));
    } else {
      result.push_back(Encode(static_cast<T>(values.Get(index))));
    }
  }
  return result;
}

template <class T, size_t N, class Storage>
auto DecodeArray(const Json& values) -> std::array<Storage, N> {
  if (!values.is_array() || values.size() != N)
    throw std::invalid_argument("invalid fixed array length");
  std::array<Storage, N> result;
  for (size_t index = 0; index < N; ++index)
    result[index] = Codec<T>::Decode(values[index]);
  return result;
}

}  // namespace tsugumi::ruby
