{
  pkgs,
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  kit,
}:
let
  fixtureEval = import ../tests/card-eval.nix { inherit pkgs; };
  common = {
    inputs = ../tests/java/interface/_inputs.fbs;
    outputs = ../tests/java/interface/_outputs.fbs;
    params = import ../tests/java/interface/params.nix;
  };
  interface = (import ../tests/../nix/card-interface.nix { inherit pkgs; }) common;
  definition = fixtureEval.callWithParamCheck ../tests/java/card { inherit (common) params; } {
    mode = 1;
  };
  card = definition.run;
  host = lib.fileset.toSource {
    root = ../tests;
    fileset = lib.fileset.unions [
      ../tests/fake_host.c
      ../tests/legacy.c
      ../tests/wire.h
    ];
  };
  inherit (stdenv.hostPlatform) isDarwin;
in
assert !(card ? views);
assert definition.views == kit.java.views;
runCommandCC "tsugumi-java-abi-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [ flatbuffers ];
    passthru.apis = [ definition.api ];
  }
  ''
    card=${card}/lib/main.so
    include=${kit.cppRuntime}/include
    test -s ${definition.api}/share/tsugumi/api/tsugumi/Card.java
    test -s ${definition.api}/share/tsugumi/api/inputs/arrival/tsugumi/merge/t0/TemperatureT.java
    grep -Eq 'TemperatureT \| .*\.ReadingT' ${definition.api}/share/tsugumi/api/README.md
    for view in ${lib.escapeShellArgs (map (view: "${view.blob}") definition.views)}; do
      test -s "$view"
    done
    for schema in _inputs _outputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . ${interface}/"$schema.fbs"
    done
    $CC -std=c11 -D_DEFAULT_SOURCE -DIN_CALL_DISPLAY_COUNT=7 -Dmain=check_shared_abi \
      -Wall -Wextra -Werror -I"$include" -c ${host}/fake_host.c
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared ${host}/legacy.c -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -I${host} -I"$include" \
      ${../tests/java/wire.cc} fake_host.o -pthread ${
        lib.optionalString (!isDarwin) "-ldl"
      } -o fake_host

    test -L ${card}/bin/tsugumi-test-sort
    test ! -e ${card}/bin/sh
    mkdir work tmp
    legacy="$PWD/legacy.so"
    runner="$PWD/fake_host"
    tmp="$PWD/tmp"
    cd work
    env -i HOME="$PWD" TMPDIR="$tmp" "$runner" "$card" "$legacy"

    ${
      if isDarwin then
        ''test "$(nm -gUj "$card")" = _tsugumi_main''
      else
        ''test "$(nm -D --defined-only -j "$card")" = tsugumi_main''
    }
    touch "$out"
  ''
