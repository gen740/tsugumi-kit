{
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  coreutils,
  kit,
}:
let
  fixture = ../sdk/js/tests;
  contract = fixture + "/contract";
  sdk =
    (kit.bind {
      inherit contract;
      definitionDir = fixture;
    }).jsSdk;
  args = {
    src = fixture + "/src";
  };
  card = sdk.jsCard (args // { tools = [ coreutils ]; });
  withoutTools = sdk.jsCard args;
  tests = lib.fileset.toSource {
    root = ../tests;
    fileset = lib.fileset.unions [
      ../tests/fake_host.c
      ../tests/legacy.c
      ../tests/wire.cc
      ../tests/wire.h
    ];
  };
  inherit (stdenv.hostPlatform) isDarwin;
in
assert sdk.views == kit.cppSdk.views;
runCommandCC "tsugumi-js-abi9-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [ flatbuffers ];
  }
  ''
      test -L "${card}/bin/sort"
      test ! -e "${card}/bin/sh"
    test ! -e "${withoutTools}/bin"
      for view in ${lib.escapeShellArgs (map (view: "${view.blob}") sdk.views)}; do
        test -s "$view"
      done
      library=${card}/lib/main.so
      include=${kit.cppRuntime}/include
      for schema in _inputs _outputs _params; do
        flatc --cpp --gen-object-api --scoped-enums -o . "${contract}/$schema.fbs"
      done
      $CC -std=c11 -D_DEFAULT_SOURCE -DIN_CALL_DISPLAY_COUNT=7 \
        -Wall -Wextra -Werror -I"$include" -c "${tests}/fake_host.c"
      $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "${tests}/legacy.c" -o legacy.so
      cxx=(-std=c++2c -Wall -Wextra -Werror -I. -I"${tests}" -I"$include" -I${../sdk/cpp/src})
      $CXX "''${cxx[@]}" -DCPP_DISPLAY_WIRE \
      -Dcheck_card_failure=unused_card_failure -Dcheck_success=check_shared_success \
        -c "${tests}/wire.cc" -o shared_wire.o
      $CXX "''${cxx[@]}" -c "${fixture}/wire.cc" -o js_wire.o
      link=("${kit.cppRuntime}/lib/libtsugumi_abi9.a" -pthread ${lib.optionalString (!isDarwin) "-ldl"})
      $CXX fake_host.o shared_wire.o js_wire.o "''${link[@]}" -o fake_host
      $CXX "''${cxx[@]}" "${fixture}/files.cc" shared_wire.o js_wire.o "''${link[@]}" -o file_host
      mkdir work
      (cd work && env -u PATH HOME="$PWD" ../fake_host "$library" "$PWD/../legacy.so")
      (cd work && env -u PATH HOME="$PWD" ../file_host "${withoutTools}/lib/main.so")
      ${
        if isDarwin then
          ''
            test "$(nm -gUj "$library")" = _tsugumi_main
            otool -L "$library" | grep libnode
          ''
        else
          ''
            test "$(nm -D --defined-only -j "$library")" = tsugumi_main
            readelf -d "$library" | grep libnode
          ''
      }
      touch "$out"
  ''
