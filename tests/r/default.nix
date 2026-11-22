{
  pkgs,
  flatbuffers,
  lib,
  runCommandCC,
  stdenv,
  # The kit's root entry, `import ./. { inherit pkgs; }`.
  kit,
}:
let
  runtime = kit.cppRuntime;
  definition = import ./card {
    inherit pkgs;
    inputs.tsugumi = kit;
    params = { };
    schemas = ./schemas;
  };
  card = definition.run;
  starter =
    (import ../../templates/r-write-text/write {
      inherit pkgs;
      inputs.tsugumi = kit;
      params = { };
      schemas = ../templates/text-to-path;
    }).run;
  inherit (stdenv.hostPlatform) isDarwin;
in
assert definition.views == kit.rSdk.views;
runCommandCC "tsugumi-r-abi-check"
  {
    buildInputs = [ flatbuffers ];
    passthru.apiCards = [
      card
      starter
    ];
  }
  ''
    api=${card}/share/tsugumi/api
    test -s "$api/README.md" && test -s "$api/_outputs.fbs" && test -s "$api/card.R"
    grep -F '`params` — `list <inputs.rtest.Number> or NULL`' "$api/README.md"
    grep -F '`arrival` — `list(type = "Temperature"' "$api/README.md"
    card=${card}/lib/main.so
    include=${runtime}/include
    cp ${../fake_host.c} fake_host.c
    cp ${../wire.h} wire.h
    $CC -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
      -I"$include" -I. -pthread -c ${./host.c} -o host.o
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "${../legacy.c}" -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -isystem "${card.generated}/include" -I"$include" \
      ${./wire.cc} host.o -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake-host
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -isystem "${starter.generated}/include" \
      -I"$include" ${../julia/text_template.cc} ${lib.optionalString (!isDarwin) "-ldl"} -o text-host

    mkdir home
    pushd home
    export HOME="$PWD"
    mkdir scratch directory
    export TMPDIR="$PWD/scratch"
    printf 'input\n' > input.txt
    printf 'directory\n' > directory/child
    touch scratch/clear-me
    ../fake-host "$card" "$OLDPWD/legacy.so"
    ../text-host "${starter}/lib/main.so"
    popd

    ${
      if isDarwin then
        ''
          test "$(nm -gUj "$card")" = _tsugumi_main
        ''
      else
        ''
          test "$(nm -D --defined-only -j "$card")" = tsugumi_main
        ''
    }
    strings -a "$card" > card.strings
    nm "$card" | c++filt > card.symbols
    if grep -Ei 'BFBS|reflection_generated|flatbuffers.*reflection|reflection::' card.strings card.symbols; then
      echo "Unexpected reflection data in main.so" >&2
      exit 1
    fi
    touch "$out"
  ''
