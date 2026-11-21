{
  lib,
  stdenv,
  cmake,
  ninja,
  clang-tools,
  flatbuffers,
  nlohmann_json,
  abiHeaders,
}:
stdenv.mkDerivation {
  pname = "tsugumi-cpp-runtime";
  version = "9";
  src = ../sdk/cpp;
  nativeBuildInputs = [
    cmake
    ninja
  ]
  ++ lib.optional stdenv.hostPlatform.isLinux clang-tools;
  buildInputs = [ nlohmann_json ];
  propagatedBuildInputs = [ flatbuffers ];
  # The pinned Darwin clang-tidy wrapper fails to parse the Apple SDK's
  # __has_feature checks. Run the same SDK analysis in Linux builds.
  cmakeFlags = [
    "-DTSUGUMI_ABI_INCLUDE=${abiHeaders}/include"
  ]
  ++ lib.optional stdenv.hostPlatform.isLinux "-DTSUGUMI_CLANG_TIDY=ON";
  postInstall = ''
    cp -r "${abiHeaders}/include/." "$out/include/"
  '';
}
