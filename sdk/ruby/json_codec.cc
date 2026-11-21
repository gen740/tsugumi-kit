#include "json_codec.hh"

namespace tsugumi::ruby {
namespace {

constexpr std::string_view kBase64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

auto CheckObject(const Json& value,
                 std::initializer_list<std::string_view> fields) -> void {
  if (!value.is_object()) throw std::invalid_argument("expected an object");
  for (const auto& [key, unused] : value.items()) {
    if (std::ranges::find(fields, key) == fields.end())
      throw std::invalid_argument("unknown field " + key);
  }
}

auto EncodeBytes(const std::vector<uint8_t>& bytes) -> std::string {
  std::string encoded;
  encoded.reserve((bytes.size() + 2) / 3 * 4);
  for (size_t i = 0; i < bytes.size(); i += 3) {
    const bool second = i + 1 < bytes.size();
    const bool third = i + 2 < bytes.size();
    const uint32_t bits =
        (static_cast<uint32_t>(bytes[i]) << 16) |
        (second ? static_cast<uint32_t>(bytes[i + 1]) << 8 : 0) |
        (third ? bytes[i + 2] : 0);
    encoded += kBase64[(bits >> 18) & 63];
    encoded += kBase64[(bits >> 12) & 63];
    encoded += second ? kBase64[(bits >> 6) & 63] : '=';
    encoded += third ? kBase64[bits & 63] : '=';
  }
  return encoded;
}

auto DecodeBytes(std::string_view encoded) -> std::vector<uint8_t> {
  if (encoded.size() % 4 != 0)
    throw std::invalid_argument("invalid base64 length");
  std::vector<uint8_t> bytes;
  bytes.reserve(encoded.size() / 4 * 3);
  for (size_t i = 0; i < encoded.size(); i += 4) {
    const bool last = i + 4 == encoded.size();
    const auto a = kBase64.find(encoded[i]);
    const auto b = kBase64.find(encoded[i + 1]);
    const auto c = kBase64.find(encoded[i + 2]);
    const auto d = kBase64.find(encoded[i + 3]);
    const bool two_pad = last && encoded[i + 2] == '=' && encoded[i + 3] == '=';
    const bool one_pad = last && encoded[i + 3] == '=' && c != kBase64.npos;
    if (a == kBase64.npos || b == kBase64.npos ||
        (c == kBase64.npos && !two_pad) ||
        (d == kBase64.npos && !two_pad && !one_pad) || (two_pad && (b & 15)) ||
        (one_pad && (c & 3)))
      throw std::invalid_argument("invalid base64");
    bytes.push_back(static_cast<uint8_t>((a << 2) | (b >> 4)));
    if (!two_pad) bytes.push_back(static_cast<uint8_t>((b << 4) | (c >> 2)));
    if (!two_pad && !one_pad)
      bytes.push_back(static_cast<uint8_t>((c << 6) | d));
  }
  return bytes;
}

}  // namespace tsugumi::ruby
