{
  lib,
  pkgs,
  stdenv,
  runCommandCC,
  flatbuffers,
  kit,
}:
let
  fixtureEval = import ../card-eval.nix { inherit pkgs; };
  common = {
    inputs = fixtureEval.inputs (./ports);
    outputs = ./interface/_outputs.fbs;
    params = ./interface/params.nix;
  };
  interface = (import ../../nix/card-interface.nix { inherit pkgs; }) common;
  sharedInterface = (import ../../nix/card-interface.nix { inherit pkgs; }) {
    inputs = { inherit (common.inputs) number file directory; };
    outputs = ../contract/_outputs.fbs;
    params = ../contract/params.nix;
  };
  definition = fixtureEval.call ./definition.nix { inherit (common) inputs; };
  card = definition.run;
  headers = pkgs.callPackage ../../nix/abi-headers.nix { };
  inherit (stdenv.hostPlatform) isDarwin;
in
runCommandCC "tsugumi-rust-abi-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [ flatbuffers ];
    passthru.apis = [ definition.api ];
  }
  ''
    card=${card}/lib/main.so
    include=${headers}/include
    api=${definition.api}/share/tsugumi/api
    test -s "$api/lib.rs"
    test -s "$api/_inputs_generated.rs"
    grep -q '## Params' "$api/README.md"

    cp ${../fake_host.c} shared_fake_host.c
    cp ${../wire.h} wire.h
    for schema in _inputs _outputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . ${interface}/"$schema.fbs"
      flatc --cpp --gen-object-api --scoped-enums -o cpp ${sharedInterface}/"$schema.fbs"
    done

    $CC -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I. -I"$include" \
      -c ${./fake_host.c} -o fake_host.o
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared ${../legacy.c} -o legacy.so
    # Reuse the shared display assertions. The Rust fixture checks its own ports.
    $CXX -std=c++2c -Wall -Wextra -Werror -Icpp -I. -I"$include" \
      -Dmake_input=cpp_make_input -Dcheck_success=cpp_check_success \
      -Dcheck_input_failure=cpp_check_input_failure -Dcheck_card_failure=cpp_check_card_failure \
      -c ${../wire.cc} -o shared_wire.o
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -I"$include" \
      ${./wire.cc} fake_host.o shared_wire.o -pthread ${
        lib.optionalString (!isDarwin) "-ldl"
      } -o fake_host
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
    if ! grep -q 'libz\.' deps; then
      echo "Missing dynamic zlib dependency in main.so" >&2
      exit 1
    fi
    if grep -Ei 'flatbuffers|libtsugumi' deps; then
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
