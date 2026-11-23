{
  lib,
  stdenv,
  pkgs,
  runCommand,
  runCommandCC,
  flatbuffers,
  go,
  kit,
  runtime,
}:
let
  fixtureEval = import ../card-eval.nix { inherit pkgs; };
  common = {
    inputs = ./interface/_inputs.fbs;
    outputs = ./interface/_outputs.fbs;
    params = import ./interface/params.nix;
  };
  interface = (import ../../nix/card-interface.nix { inherit pkgs; }) common;
  definition = fixtureEval.callWithParamCheck ./default.nix { inherit (common) params; } {
    mode = 1;
  };
  card = definition.run;
  outputOnlyDefinition = fixtureEval.call ./output-only { };
  outputCommon = {
    inputs = ./output-only/_inputs.fbs;
    outputs = ./output-only/_outputs.fbs;
    params = import ./output-only/params.nix;
  };
  outputInterface = (import ../../nix/card-interface.nix { inherit pkgs; }) outputCommon;
  developmentSdk = outputOnlyDefinition.run.sdk;
  developmentSrc = runCommand "tsugumi-go-development-source" { } ''
    mkdir "$out"
    cp -r ${./output-only/src}/. "$out/"
    ln -s ${developmentSdk} "$out/tsugumi"
  '';
  outputOnly = kit.go.goCard (outputCommon // { src = developmentSrc; });
  inherit (stdenv.hostPlatform) isDarwin;
in
runCommandCC "tsugumi-go-abi-check"
  {
    nativeBuildInputs = [
      flatbuffers
      go
    ];
    buildInputs = [ flatbuffers ];
    passthru.apis = [
      definition.api
      outputOnlyDefinition.api
    ];
  }
  ''
    card=${card}/lib/main.so
    payload=${card}/lib/go-card.so
    for api in ${definition.api}/share/tsugumi/api ${outputOnlyDefinition.api}/share/tsugumi/api; do
      test -s "$api/card.go"
      test -s "$api/parameters/Params.go"
      grep -q '## Params' "$api/README.md"
    done
    shared=${../.}
    include=${runtime}/include

    for schema in _inputs _outputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . ${interface}/"$schema.fbs"
    done
    $CC -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I"$include" -c "$shared/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$shared/legacy.c" -o legacy.so
    $CC -std=c11 -Wall -Wextra -Werror -I"$include" -I"$shared" -c ${./cancel.c} -o cancel.o
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -I"$shared" -I"$include" -c ${./wire.cc} -o wire.o
    $CXX wire.o fake_host.o -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake_host
    $CXX wire.o cancel.o ${lib.optionalString (!isDarwin) "-ldl"} -o cancel
    ./fake_host "$card" "$PWD/legacy.so"
    ./cancel "$card"

    mkdir output-only
    flatc --cpp --scoped-enums -o output-only ${outputInterface}/{_outputs,_params}.fbs
    $CXX -std=c++2c -Wall -Wextra -Werror -Ioutput-only -I"$shared" -I"$include" \
      ${./output-only/startup.cc} ${lib.optionalString (!isDarwin) "-ldl"} -o startup
    ./startup ${outputOnly}/lib/{main,go-card}.so

    cp -r --no-preserve=mode ${developmentSrc} dev
    (cd dev
      export CGO_ENABLED=1 GOTOOLCHAIN=local GOWORK=off GOPROXY=off
      export GOCACHE="$TMPDIR/go-cache" GOMODCACHE="$TMPDIR/go-modules"
      go test -mod=readonly ./...
    )

    ${
      if isDarwin then
        ''
          test "$(nm -gUj "$card")" = _tsugumi_main
          test "$(nm -gUj "$payload")" = _tsugumi_go_entry
          otool -L "$card" > loader.deps
          otool -L "$payload" | tail -n +2 > payload.deps
        ''
      else
        ''
          test "$(nm -D --defined-only -j "$card")" = tsugumi_main
          test "$(nm -D --defined-only -j "$payload")" = tsugumi_go_entry
          readelf -d "$card" > loader.deps
          readelf -d "$payload" > payload.deps
        ''
    }
    grep -E 'libz\.' payload.deps
    if grep -E 'go-card\.so|libz\.' loader.deps || \
       grep -Ei 'flatbuffers|nlohmann|libtsugumi' loader.deps payload.deps; then
      echo "Unexpected dynamic dependency in the Go Card" >&2
      exit 1
    fi
    strings -a "$card" "$payload" > card.strings
    nm "$card" "$payload" | c++filt > card.symbols
    if grep -Ei 'BFBS|reflection_generated|flatbuffers.*reflection|reflection::' card.strings card.symbols; then
      echo "Unexpected FlatBuffers reflection data in the Go Card" >&2
      exit 1
    fi
    touch "$out"
  ''
