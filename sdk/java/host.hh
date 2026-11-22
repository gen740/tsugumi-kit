#pragma once

#include <span>
#include <string_view>

#include "adapter.hh"

namespace tsugumi::abi9 {

using Verify = auto (*)(const Buffer&) -> bool;

template <class Table>
auto VerifyJavaTable(const Buffer& buffer) -> bool {
  return buffer.Verify<Table>() != nullptr;
}

struct JavaPort {
  std::string_view name;
  PortKind kind;
  bool required;
  Verify verify;
};

auto RunJava(tsugumi_bytes bytes, Verify params,
             std::span<const JavaPort> inputs,
             std::span<const JavaPort> outputs) -> Tsugumi::Card::CardResultT;

}  // namespace tsugumi::abi9
