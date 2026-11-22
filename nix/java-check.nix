{
  lib,
  stdenv,
  runCommand,
  runCommandCC,
  writeShellScriptBin,
  flatbuffers,
  coreutils,
  jdk21_headless,
  kit,
}:
let
  contract = ../tests/java/contract;
  definitionDir = ../tests/java;
  bound = (kit.bind { inherit contract definitionDir; }).javaSdk;
  dependency =
    runCommand "tsugumi-java-test-dependency.jar"
      {
        nativeBuildInputs = [ jdk21_headless ];
      }
      ''
        mkdir classes
        cp ${../tests/java/Offset.java} Offset.java
        javac -d classes Offset.java
        jar --create --date=1980-01-01T00:00:02Z --file "$out" -C classes .
      '';
  card = bound.javaCard {
    src = ../tests/java/src;
    jars = [ dependency ];
    tools = [
      (writeShellScriptBin "tsugumi-test-sort" ''
        exec ${coreutils}/bin/sort "$@"
      '')
    ];
  };
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
assert kit.javaSdk.views == kit.cppSdk.views;
runCommandCC "tsugumi-java-abi9-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [ flatbuffers ];
  }
  ''
    card=${card}/lib/main.so
    include=${kit.cppRuntime}/include
    for view in ${lib.escapeShellArgs (map (view: "${view.blob}") bound.views)}; do
      test -s "$view"
    done
    for schema in _inputs _outputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . ${contract}/"$schema.fbs"
    done
    $CC -std=c11 -D_DEFAULT_SOURCE -DIN_CALL_DISPLAY_COUNT=7 \
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
