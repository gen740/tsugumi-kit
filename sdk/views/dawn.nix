{
  lib,
  runCommand,
  fetchFromGitHub,
  llvmPackages,
  wabt,
}:
let
  # Match TSUGUMI_VIEW_WEBGPU_HEADERS_REV and the hosts' wasm32 layouts.
  webgpuHeaders = fetchFromGitHub {
    owner = "webgpu-native";
    repo = "webgpu-headers";
    rev = "a1dc77409bb2b8bd80540cbf4b8db219e123e909";
    hash = "sha256-hkwn8AMzkjF6j2J5qJo8FYAoMeBc0DjU0/FQddSgg/s=";
  };
in
{
  name,
  src,
  cflags ? [ ],
}:
{
  inherit name;
  type = "dawn";
  blob = "${
    runCommand "tsugumi-dawn-view-${name}" { nativeBuildInputs = [ wabt ]; } ''
      mkdir -p include/{tsugumi,webgpu} freestanding "$out"
      cp ${../../abi}/{card_abi,tsugumi_view}.h include/tsugumi/
      cp ${webgpuHeaders}/webgpu.h include/webgpu/
      cat > freestanding/math.h <<'MATH'
      #pragma once
      #define NAN __builtin_nanf("")
      #define INFINITY __builtin_inff()
      MATH

      find ${lib.escapeShellArg "${src}"} -type f -name '*.c' ! -name '._*' -print0 | LC_ALL=C sort -z > sources
      mapfile -d "" -t sources < sources
      if (( ''${#sources[@]} == 0 )); then
        echo "dawnView: no C sources in src" >&2
        exit 1
      fi
      ${llvmPackages.clang-unwrapped}/bin/clang --target=wasm32 \
        -B${llvmPackages.lld}/bin \
        -O2 -nostdlib -ffreestanding -mbulk-memory -Wl,--no-entry -Wl,--strip-all \
        -Iinclude -Ifreestanding ${lib.escapeShellArgs cflags} "''${sources[@]}" -o "$out/view.wasm"

      wasm-objdump -x "$out/view.wasm" > imports
      awk '
        /^Import\[/ { importing = 1; next }
        /^[^ ]/ { importing = 0 }
        importing && /^ - / && !/ <- (webgpu|tsugumi)\./ {
          print "dawnView: unexpected import: " $0 > "/dev/stderr"
          failed = 1
        }
        END { exit failed }
      ' imports
    ''
  }/view.wasm";
}
