{
  flatbuffers,
  lib,
  R,
  rCard,
  runCommandCC,
  runtime,
  stdenv,
}:
let
  cardSource =
    runCommandCC "tsugumi-r-test-source"
      {
        buildInputs = [
          flatbuffers
          R
        ];
      }
      ''
        mkdir -p "$out"
        cp "${../wire.h}" wire.h
        $CXX -std=c++2c -Wall -Wextra -Werror -fPIC -shared -pthread \
          -I"${runtime}/include" -I"${R}/lib/R/include" -I. \
          ${./effects.cc} -L"${R}/lib/R/lib" -lR -o "$out/effects.so"
        substitute "${./card/main.R}" "$out/main.R" \
          --replace-fail @native_effects@ "$out/effects.so"
      '';
  card = rCard {
    src = cardSource;
    contract = ./contract;
  };
  inherit (stdenv.hostPlatform) isDarwin;
in
runCommandCC "tsugumi-r-abi9-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [ flatbuffers ];
  }
  ''
    card=${card}/lib/main.so
    include=${runtime}/include
    for schema in _inputs _outputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . ${./contract}/"$schema.fbs"
    done
    cp ${../fake_host.c} fake_host.c
    cp ${../wire.h} wire.h
    $CC -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
      -I"$include" -I. -pthread -c ${./host.c} -o host.o
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "${../legacy.c}" -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -I"$include" \
      ${./wire.cc} host.o -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake-host

    mkdir home
    pushd home
    export HOME="$PWD"
    mkdir scratch directory
    export TMPDIR="$PWD/scratch"
    printf 'input\n' > input.txt
    printf 'directory\n' > directory/child
    touch scratch/clear-me
    ../fake-host "$card" "$OLDPWD/legacy.so"
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
