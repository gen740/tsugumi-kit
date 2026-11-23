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
  inherit (stdenv.hostPlatform) isDarwin;
  shared = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../fake_host.c
      ../legacy.c
      ../wire.h
    ];
  };
  runtime = kit.cppRuntime;
  fixtureEval = import ../card-eval.nix { inherit pkgs; };
  common = {
    inputs = fixtureEval.inputs (./ports);
    outputs = ./interface/_outputs.fbs;
    params = ./interface/params.nix;
  };
  definition = fixtureEval.call ./card { inherit (common) inputs; };
  card = definition.run;
  starter = fixtureEval.call ../../templates/julia-write-text/write {
    inputs = fixtureEval.inputs ../templates/text-to-path;
  };
in
assert definition.views == kit.julia.views;
runCommandCC "tsugumi-julia-abi-check"
  {
    buildInputs = [ flatbuffers ];
    passthru.apis = [
      definition.api
      starter.api
    ];
  }
  ''
    shared=${shared}
    include=${runtime}/include
    card=${card}/lib/main.so

    test -s "${definition.api}/share/tsugumi/api/README.md"
    test -s "${definition.api}/share/tsugumi/api/_inputs.fbs"
    test -s "${definition.api}/share/tsugumi/api/card.jl"
    grep -F '`arrival` — `(type="Temperature"' "${definition.api}/share/tsugumi/api/README.md"
    $CC -std=c11 -D_DEFAULT_SOURCE -Dmain=shared_main -Dalarm=julia_test_alarm -Wall -Wextra -Werror \
      -I"$include" -c "$shared/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -c ${./watchdog.c} -o watchdog.o
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$shared/legacy.c" -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -isystem "${card.generated}/include" -I"$include" -I"$shared" \
      -I${../../sdk/cpp/src} \
      ${./wire.cc} fake_host.o watchdog.o ${runtime}/lib/libtsugumi_abi.a \
      -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake_host

    mkdir work scratch
    export HOME="$PWD/work" TMPDIR="$PWD/scratch"
    export JULIA_NUM_THREADS=1,1
    printf 'card input\n' > "$HOME/input.txt"
    mkdir "$HOME/input-directory"
    printf 'directory input\n' > "$HOME/input-directory/keep.txt"
    (cd "$HOME" && ../fake_host "$card" "$OLDPWD/legacy.so")
    test -d "$HOME/.julia"

    $CXX -std=c++2c -Wall -Wextra -Werror -isystem "${starter.run.generated}/include" \
      -I"$include" -I"$shared" ${./text_template.cc} \
      ${lib.optionalString (!isDarwin) "-ldl"} -o text-host
    (cd "$HOME" && ../text-host "${starter.run}/lib/main.so")

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
    nm "$card" | c++filt > card.symbols
    strings -a "$card" > card.strings
    if grep -Ei 'BFBS|reflection_generated|flatbuffers.*reflection|reflection::' card.symbols card.strings; then
      echo "Unexpected reflection data in main.so" >&2
      exit 1
    fi
    touch "$out"
  ''
