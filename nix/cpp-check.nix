{
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  zlib,
  cppCard,
  runtime,
}:
let
  contract = ../tests/contract;
  card = cppCard {
    src = ../tests/card;
    inherit contract;
    buildInputs = [ zlib ];
    cxxflags = [
      "-DABI9_TEST_CARD"
      "-lz"
    ];
  };
  inherit (stdenv.hostPlatform) isDarwin;
in
assert
  builtins.functionArgs cppCard == {
    src = false;
    contract = false;
    buildInputs = true;
    cxxflags = true;
  };
runCommandCC "tsugumi-cpp-abi9-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [ flatbuffers ];
  }
  ''
    card=${card}/lib/main.so
    tests=${../tests}
    include=${runtime}/include

    for schema in _inputs _outputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . ${contract}/"$schema.fbs"
    done
    $CC -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I"$include" -c "$tests/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$tests/legacy.c" -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -I"$include" \
      "$tests/wire.cc" fake_host.o -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake_host
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
    if grep -Ei 'flatbuffers|nlohmann|libtsugumi' deps; then
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
