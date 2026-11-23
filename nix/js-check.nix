{
  pkgs,
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  # The kit's root entry, `import ./. { inherit pkgs; }`.
  kit,
}:
let
  fixture = ../sdk/js/tests;
  fixtureEval = import ../tests/card-eval.nix { inherit pkgs; };
  common = {
    inputs = fixtureEval.inputs (fixture + "/ports");
    outputs = fixture + "/interface/_outputs.fbs";
    params = fixture + "/interface/params.nix";
  };
  interface = (import ../tests/../nix/card-interface.nix { inherit pkgs; }) common;
  sdk = kit.js;
  definition = fixtureEval.call fixture { inherit (common) inputs; };
  card = definition.run;
  withoutTools = sdk.jsCard {
    inherit (common) inputs outputs params;
    src = fixture + "/src";
  };
  note = fixtureEval.call ../templates/js-write-note/note { };
  noteInterface = (import ./card-interface.nix { inherit pkgs; }) {
    inputs = { };
    outputs = ../templates/js-write-note/note/_outputs.fbs;
    params = ../templates/js-write-note/note/params.nix;
  };
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
assert definition.views == kit.js.views;
assert note.views == kit.js.views;
runCommandCC "tsugumi-js-abi-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [ flatbuffers ];
    passthru.apis = [
      definition.api
      note.api
    ];
  }
  ''
    test -L "${card}/bin/sort"
    test ! -e "${card}/bin/sh"
    test ! -e "${withoutTools}/bin"
    for view in ${lib.escapeShellArgs (map (view: "${view.blob}") definition.views)}; do
      test -s "$view"
    done
    api=${definition.api}/share/tsugumi/api
    for file in README.md bindings.ts index.d.ts _inputs.ts _outputs.ts _params.ts; do
      test -s "$api/$file"
    done
    test ! -e "$api/tsugumi/card"
    grep -F '"arrival"?: inputs.arrival.tsugumi.merge.t0.TemperatureT | inputs.arrival.tsugumi.merge.t1.ReadingT;' "$api/bindings.ts"
    grep -F '"file"?: string;' "$api/bindings.ts"
    grep -F -- '- `number` — `outputs.number.NumberT (required output)`' "$api/README.md"
    grep -F -- '- `weather` — `outputs.weather.ReadingT | undefined`' "$api/README.md"

    library=${card}/lib/main.so
    include=${kit.cppRuntime}/include
    for schema in _inputs _outputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . "${interface}/$schema.fbs"
    done
    $CC -std=c11 -D_DEFAULT_SOURCE -DIN_CALL_DISPLAY_COUNT=7 \
      -Wall -Wextra -Werror -I"$include" -c "${tests}/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "${tests}/legacy.c" -o legacy.so
    cxx=(-std=c++2c -Wall -Wextra -Werror -I. -I"${tests}" -I"$include" -I${../sdk/cpp/src})
    $CXX "''${cxx[@]}" -DCPP_DISPLAY_WIRE \
      -Dcheck_card_failure=unused_card_failure -Dcheck_success=check_shared_success \
      -c "${tests}/wire.cc" -o shared_wire.o
    $CXX "''${cxx[@]}" -c "${fixture}/wire.cc" -o js_wire.o
    link=("${kit.cppRuntime}/lib/libtsugumi_abi.a" -pthread ${lib.optionalString (!isDarwin) "-ldl"})
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

    # The note template writes its parameter to a Path output.
    mkdir note note-schemas
    flatc --cpp --gen-object-api -o note-schemas ${noteInterface}/_params.fbs
    $CXX -std=c++2c -Wall -Wextra -Werror -Inote-schemas -I"${tests}" -I"$include" -I${../sdk/cpp/src} \
      "${fixture}/note.cc" "${kit.cppRuntime}/lib/libtsugumi_abi.a" \
      -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o note_host
    (cd note && env -u PATH HOME="$PWD" ../note_host "${note.run}/lib/main.so")
    touch "$out"
  ''
