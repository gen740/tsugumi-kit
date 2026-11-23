{
  pkgs,
  lib,
  runCommandCC,
  stdenv,
  flatbuffers,
  # The kit's root entry, `import ./. { inherit pkgs; }`.
  kit,
}:
let
  inherit (stdenv.hostPlatform) isDarwin;
  fixtureEval = import ../card-eval.nix { inherit pkgs; };
  common = {
    inputs = fixtureEval.inputs (./ports);
    outputs = ./interface/_outputs.fbs;
    params = import ./interface/params.nix;
  };
  interface = (import ../../nix/card-interface.nix { inherit pkgs; }) common;
  definition = fixtureEval.callWithParamCheck ./card { inherit (common) inputs params; } {
    mode = 1;
  };
  card = definition.run;
in
assert definition.views == kit.csharp.views;
runCommandCC "tsugumi-csharp-abi-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [ flatbuffers ];
    passthru.apis = [ definition.api ];
  }
  ''
    card=${card}/lib/main.so
    shared=${../.}
    include=${kit.cppRuntime}/include

    test -s ${definition.api}/share/tsugumi/api/Card.g.cs
    grep -q 'Input.Arrival: object' ${definition.api}/share/tsugumi/api/README.md
    for schema in _inputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . ${interface}/"$schema.fbs"
    done
    # The fake host only reads outputs. The C++ object API's Pack method also
    # conflicts with the fixture's Pack table name.
    flatc --cpp --scoped-enums -o . ${interface}/_outputs.fbs
    $CC -std=c11 -D_DEFAULT_SOURCE -Dmain=check_shared_abi -Wall -Wextra -Werror \
      -I"$include" -c "$shared/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$shared/legacy.c" -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -I"$include" -I"$shared" \
      ${./wire.cc} fake_host.o -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake_host

    printf 'input contents' > input.txt
    mkdir input-dir
    DOTNET_EnableDiagnostics=1 DOTNET_EnableDiagnostics_IPC=0 \
      ./fake_host "$card" "$PWD/legacy.so"

    ${
      if isDarwin then
        ''
          test "$(nm -gUj "$card")" = _tsugumi_main
          otool -L "$card" > deps
        ''
      else
        ''
          test "$(nm -D --defined-only -j "$card")" = tsugumi_main
          readelf -d "$card" > deps
        ''
    }
    if grep -Ei 'flatbuffers|nlohmann|libtsugumi|libhostfxr|libcoreclr|libhostpolicy' deps; then
      echo "Unexpected SDK shared-library dependency" >&2
      exit 1
    fi
    strings -a "$card" > card.strings
    nm "$card" | c++filt > card.symbols
    if grep -Ei 'BFBS|reflection_generated|flatbuffers.*reflection|reflection::' card.strings card.symbols; then
      echo "Unexpected reflection data in main.so" >&2
      exit 1
    fi
    touch "$out"
  ''
