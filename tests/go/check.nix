{
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  goCard,
  runtime,
}:
let
  contract = ./contract;
  card = goCard {
    src = ./src;
    inherit contract;
  };
  inherit (stdenv.hostPlatform) isDarwin;
in
runCommandCC "tsugumi-go-abi9-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [ flatbuffers ];
  }
  ''
    card=${card}/lib/main.so
    shared=${../.}
    include=${runtime}/include

    for schema in _inputs _outputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . ${contract}/"$schema.fbs"
    done
    $CC -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I"$include" -c "$shared/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$shared/legacy.c" -o legacy.so
    $CC -std=c11 -Wall -Wextra -Werror -I"$include" -I"$shared" -c ${./cancel.c} -o cancel.o
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -I"$shared" -I"$include" -c ${./wire.cc} -o wire.o
    $CXX wire.o fake_host.o -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake_host
    $CXX wire.o cancel.o ${lib.optionalString (!isDarwin) "-ldl"} -o cancel
    ./fake_host "$card" "$PWD/legacy.so"
    ./cancel "$card"

    ${
      if isDarwin then
        ''test "$(nm -gUj "$card")" = _tsugumi_main''
      else
        ''test "$(nm -D --defined-only -j "$card")" = tsugumi_main''
    }
    strings -a "$card" > card.strings
    nm "$card" | c++filt > card.symbols
    if grep -Ei 'BFBS|reflection_generated|flatbuffers.*reflection|reflection::' card.strings card.symbols; then
      echo "Unexpected FlatBuffers reflection data in main.so" >&2
      exit 1
    fi
    touch "$out"
  ''
