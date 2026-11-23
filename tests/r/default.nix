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
  fixtureEval = import ../card-eval.nix { inherit pkgs; };
  common = {
    inputs = fixtureEval.inputs (./ports);
    outputs = ./interface/_outputs.fbs;
    params = ./interface/params.nix;
  };
  definition = fixtureEval.call ./card { inherit (common) inputs; };
  card = definition.run;
  starter = fixtureEval.call ../../templates/r-write-text/write {
    inputs = fixtureEval.inputs ../templates/text-to-path;
  };
  inherit (stdenv.hostPlatform) isDarwin;
in
assert definition.views == kit.r.views;
runCommandCC "tsugumi-r-abi-check"
  {
    buildInputs = [ flatbuffers ];
    passthru.apis = [
      definition.api
      starter.api
    ];
  }
  ''
    api=${definition.api}/share/tsugumi/api
    test -s "$api/README.md" && test -s "$api/_outputs.fbs" && test -s "$api/card.R"
    grep -F '`params` — `list <inputs.params.Number> or NULL`' "$api/README.md"
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
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -isystem "${starter.run.generated}/include" \
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
    ../text-host "${starter.run}/lib/main.so"
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
