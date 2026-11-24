{
  lib,
  runCommand,
  fetchFromGitHub,
  fetchurl,
  llvmPackages,
  python3,
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
  notoSansJP = fetchurl {
    url = "https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004/Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf";
    hash = "sha256-aKP8mIALKiezcfL7eZkdrzYzvYkwnU/6ppRv1YfzdbU=";
  };
  notoSansLicense = fetchurl {
    url = "https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004/LICENSE";
    hash = "sha256-anP5VBwt50FYwOfPawpY73dPWngL8ZHy1+ycxT7+K/I=";
  };
  fontPython = python3.withPackages (ps: [
    ps.fonttools
    ps.pillow
  ]);
in
{
  name,
  src,
  cflags ? [ ],
  font ? null,
}:
let
  pixelSize = font.pixelSize or 24;
  characters = font.characters or null;
  charactersFile = builtins.toFile "dawn-font-characters.txt" characters;
in
assert font == null || (builtins.isInt pixelSize && pixelSize >= 8 && pixelSize <= 128);
{
  inherit name;
  type = "dawn";
  blob = "${
    runCommand "tsugumi-dawn-view-${name}" { nativeBuildInputs = [ wabt ]; } ''
      mkdir -p include/{tsugumi,webgpu} freestanding "$out"
      cp ${../../abi}/{card_abi,tsugumi_view}.h include/tsugumi/
      cp ${webgpuHeaders}/webgpu.h include/webgpu/
      cp ${./dawn_helpers.h} include/tsugumi/dawn_helpers.h
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
      cc=(${llvmPackages.clang-unwrapped}/bin/clang --target=wasm32
          -B${llvmPackages.lld}/bin -O2 -ffreestanding -mbulk-memory
          -Iinclude -Ifreestanding)
      ${lib.optionalString (font != null) ''
        cp ${./dawn_font.h} include/tsugumi/dawn_font.h
        cp ${./dawn_font.c} dawn_font.c
        ${fontPython}/bin/python ${./dawn_font.py} ${notoSansJP} ${toString pixelSize} \
          --license ${notoSansLicense} \
          ${lib.optionalString (characters != null) "--characters ${charactersFile}"}
        "''${cc[@]}" -std=c23 -Wall -Wextra -Werror -c dawn_font.c -o dawn_font.o
        sources+=(dawn_font.o)
      ''}
      "''${cc[@]}" -nostdlib -Wl,--no-entry -Wl,--strip-all \
        ${lib.escapeShellArgs cflags} "''${sources[@]}" -o "$out/view.wasm"

      ${lib.optionalString (font != null) ''
        cat font-license.wasm-section >> "$out/view.wasm"
      ''}
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
