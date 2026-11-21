{
  lib,
  runCommandCC,
  stdenv,
  flatbuffers,
  csharpCard,
  cppRuntime,
}:
let
  inherit (stdenv.hostPlatform) isDarwin;
  contract = ./contract;
  card = csharpCard {
    src = ./card;
    inherit contract;
  };
in
runCommandCC "tsugumi-csharp-abi9-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [ flatbuffers ];
  }
  ''
    card=${card}/lib/main.so
    shared=${../.}
    include=${cppRuntime}/include

    for schema in _inputs _outputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . ${contract}/"$schema.fbs"
    done
    $CC -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I"$include" -c "$shared/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$shared/legacy.c" -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -I"$include" -I"$shared" \
      ${./wire.cc} fake_host.o -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake_host

    printf 'input contents' > input.txt
    mkdir input-dir
    ./fake_host "$card" "$PWD/legacy.so"

    ${
      if isDarwin then
        ''test "$(nm -gUj "$card")" = _tsugumi_main''
      else
        ''test "$(nm -D --defined-only -j "$card")" = tsugumi_main''
    }
    strings -a "$card" > card.strings
    if grep -Ei 'BFBS|reflection_generated|flatbuffers.*reflection|reflection::' card.strings; then
      echo "Unexpected reflection data in main.so" >&2
      exit 1
    fi
    touch "$out"
  ''
