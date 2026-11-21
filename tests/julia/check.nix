{
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  juliaCard,
  runtime,
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
  card = juliaCard {
    src = ./card;
    contract = ./contract;
  };
in
runCommandCC "tsugumi-julia-abi9-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [ flatbuffers ];
  }
  ''
    shared=${shared}
    include=${runtime}/include
    card=${card}/lib/main.so

    mkdir generated
    for schema in _inputs _outputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o generated ${./contract}/"$schema.fbs"
    done
    $CC -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I"$include" -c "$shared/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$shared/legacy.c" -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -isystem generated -I"$include" -I"$shared" \
      -I${../../sdk/cpp/src} \
      ${./wire.cc} fake_host.o ${runtime}/lib/libtsugumi_abi9.a \
      -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake_host

    mkdir work scratch
    export HOME="$PWD/work" TMPDIR="$PWD/scratch"
    export JULIA_NUM_THREADS=1,1
    printf 'card input\n' > "$HOME/input.txt"
    (cd "$HOME" && ../fake_host "$card" "$OLDPWD/legacy.so")
    test -d "$HOME/.julia"

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
