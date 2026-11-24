#pragma once

#include <span>
#include <string_view>

#include "adapter.hh"

namespace tsugumi::abi {

template <class Table>
auto VerifyJavaTable(const Buffer& buffer) -> bool {
  return buffer.Verify<Table>() != nullptr;
}

template <class Table, bool (*Valid)(const typename Table::NativeTableType&)>
auto ValidJavaOutput(const Buffer& buffer) -> bool {
  const auto* table = buffer.Verify<Table>();
  if (!table) return false;
  typename Table::NativeTableType value;
  table->UnPackTo(&value);
  return Valid(value);
}

struct JavaInput {
  std::string_view name;
  std::string_view schema;
  bool (*verify)(const Buffer&);
};

struct JavaOutput {
  std::string_view name;
  bool required;
  bool (*valid)(const Buffer&);
};

auto RunJava(tsugumi_bytes bytes, std::span<const JavaInput> inputs,
             std::span<const JavaOutput> outputs) -> ResultBuffer;

}  // namespace tsugumi::abi
