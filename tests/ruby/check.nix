{
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  nlohmann_json,
  rubyCard,
  runtime,
}:
let
  contract = ./contract;
  card = rubyCard {
    src = ./card;
    inherit contract;
    sourcePrefix = "src";
  };
  inherit (stdenv.hostPlatform) isDarwin;
in
runCommandCC "tsugumi-ruby-abi9-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [
      flatbuffers
      nlohmann_json
    ];
  }
  ''
    card=${card}/lib/main.so
    tests=${../.}
    include=${runtime}/include
    for schema in _inputs _outputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . ${contract}/"$schema.fbs"
    done
    $CC -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I"$include" -c "$tests/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$tests/legacy.c" -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -I"$include" -I"$tests" -I${../../sdk/cpp/src} \
      ${./wire.cc} fake_host.o ${runtime}/lib/libtsugumi_abi9.a \
      -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake_host
    ./fake_host "$card" "$PWD/legacy.so"

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
