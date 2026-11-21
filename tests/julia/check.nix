{
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  python3,
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
    nativeBuildInputs = [
      flatbuffers
      python3
    ];
    buildInputs = [ flatbuffers ];
  }
  ''
    shared=${shared}
    include=${runtime}/include
    card=${card}/lib/main.so

    python3 ${../../sdk/cpp/card_cpp.py} ${./contract}/_contract.json generated \
      ${flatbuffers.src}/reflection/reflection.fbs
    $CC -std=c11 -D_DEFAULT_SOURCE -Dmain=shared_main -Dalarm=julia_test_alarm -Wall -Wextra -Werror \
      -I"$include" -c "$shared/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -c ${./watchdog.c} -o watchdog.o
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$shared/legacy.c" -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -isystem generated/include -I"$include" -I"$shared" \
      -I${../../sdk/cpp/src} \
      ${./wire.cc} fake_host.o watchdog.o ${runtime}/lib/libtsugumi_abi9.a \
      -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake_host

    mkdir work scratch
    export HOME="$PWD/work" TMPDIR="$PWD/scratch"
    export JULIA_NUM_THREADS=1,1
    printf 'card input\n' > "$HOME/input.txt"
    mkdir "$HOME/input-directory"
    printf 'directory input\n' > "$HOME/input-directory/keep.txt"
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
