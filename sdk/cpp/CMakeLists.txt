cmake_minimum_required(VERSION 3.25)
project(tsugumi_cpp_sdk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(flatbuffers CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

option(TSUGUMI_CLANG_TIDY "Run clang-tidy when compiling the runtime" OFF)
if(TSUGUMI_CLANG_TIDY)
  find_program(CLANG_TIDY_EXECUTABLE clang-tidy REQUIRED)
  set(CMAKE_CXX_CLANG_TIDY
    "${CLANG_TIDY_EXECUTABLE};--quiet;--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy"
  )
endif()

add_library(tsugumi_abi9 STATIC src/adapter.cc src/runtime.cc)
set_target_properties(tsugumi_abi9 PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)
target_compile_definitions(tsugumi_abi9 PRIVATE NDEBUG)
target_compile_options(tsugumi_abi9 PRIVATE
  -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough
)
target_include_directories(tsugumi_abi9 PRIVATE include)
target_include_directories(tsugumi_abi9 SYSTEM PRIVATE "${TSUGUMI_ABI_INCLUDE}")
target_link_libraries(tsugumi_abi9 PRIVATE
  flatbuffers::flatbuffers
  nlohmann_json::nlohmann_json
)

install(TARGETS tsugumi_abi9 ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
