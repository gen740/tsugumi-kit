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
  schemas = ./schemas;
  definition = import ./card {
    inherit pkgs schemas;
    inputs.tsugumi = kit;
    params = { };
  };
  card = definition.run;
in
assert definition.views == kit.csharpSdk.views;
runCommandCC "tsugumi-csharp-abi-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [ flatbuffers ];
    passthru.apiCards = [ card ];
  }
  ''
    card=${card}/lib/main.so
    shared=${../.}
    include=${kit.cppRuntime}/include

    test -s ${card}/share/tsugumi/api/Card.g.cs
    grep -q 'Input.Arrival: object' ${card}/share/tsugumi/api/README.md
    for schema in _inputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . ${schemas}/"$schema.fbs"
    done
    # The fake host only reads outputs. The C++ object API's Pack method also
    # conflicts with the fixture's Pack table name.
    flatc --cpp --scoped-enums -o . ${schemas}/_outputs.fbs
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
