#pragma once

#include <dlfcn.h>

#include <filesystem>

namespace tsugumi::python {

inline auto ImagePath(const void* symbol) -> std::filesystem::path {
  Dl_info image{};
  if (!dladdr(symbol, &image) || !image.dli_fname) return {};
  std::error_code error;
  return std::filesystem::canonical(image.dli_fname, error);
}

}  // namespace tsugumi::python
