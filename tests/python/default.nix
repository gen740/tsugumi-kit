{
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  pkgs,
  kit,
}:
let
  inherit (kit) pythonSdk;
  runtime = kit.cppRuntime;
  schemas = ./schemas;
  definition =
    withTools:
    import ./card.nix {
      inherit pkgs schemas;
      inputs.tsugumi = kit;
      params = { inherit withTools; };
    };
  card = (definition false).run;
  starter = import ../../templates/python-write-text/write {
    inherit pkgs;
    inputs.tsugumi = kit;
    params = { };
    schemas = ../templates/text-to-path;
  };
  withTools = (definition true).run;
  host = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../fake_host.c
      ../legacy.c
      ../wire.h
    ];
  };
  inherit (stdenv.hostPlatform) isDarwin;
in
assert (definition false).views == pythonSdk.views && starter.views == pythonSdk.views;
assert !(card ? views);
assert
  map (view: view.name) pythonSdk.views == [
    "text"
    "markdown"
    "table"
    "image"
    "line-chart"
    "points"
    "plotly"
  ];
assert lib.all (view: view.type == "html" && view ? blob) pythonSdk.views;
runCommandCC "tsugumi-python-abi9-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [ flatbuffers ];
    passthru.apiCards = [
      card
      withTools
      starter.run
    ];
  }
  ''
    test ! -e "${card}/bin"
    card=${card}/lib/main.so
    tests=${host}
    include=${runtime}/include
    for view in ${lib.escapeShellArgs (map (view: "${view.blob}") pythonSdk.views)}; do
      test -s "$view"
    done

    for schema in _inputs _outputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . "${schemas}/$schema.fbs"
    done
    $CC -std=c11 -D_DEFAULT_SOURCE -DIN_CALL_DISPLAY_COUNT=10 \
      -Wall -Wextra -Werror -I"$include" -c "$tests/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$tests/legacy.c" -o legacy.so
    cxx=(-std=c++2c -Wall -Wextra -Werror -I. -I"$tests" -I"$include" -I${../../sdk/cpp/src})
    link=("${runtime}/lib/libtsugumi_abi9.a" -pthread ${lib.optionalString (!isDarwin) "-ldl"})
    $CXX "''${cxx[@]}" -c "${./wire.cc}" -o wire.o
    $CXX "''${cxx[@]}" fake_host.o wire.o "''${link[@]}" -o fake_host
    $CXX "''${cxx[@]}" "${./effects.cc}" wire.o "''${link[@]}" -o effects

    mkdir work effects-work tools-work 'input dir'
    printf '%s' 'sample input' > input.txt
    printf '%s' 'sample directory' > 'input dir/sample.txt'
    (cd work && HOME="$PWD" ../fake_host "$card" "$PWD/../legacy.so")
    (cd effects-work && HOME="$PWD" ../effects "$card")
    test -x "${withTools}/bin/tsugumi-python-probe"
    (cd tools-work && env -i HOME="$PWD" ../effects --tools "${withTools}/lib/main.so")
    mkdir no-tools-work
    (cd no-tools-work && env -i HOME="$PWD" ../effects --no-tools "$card")

    test -s "${starter.run}/share/tsugumi/api/card.pyi"
    api=${card}/share/tsugumi/api
    test -s "$api/card.pyi"
    test -s "$api/inputs/arrival/tsugumi/merge/t0/Temperature.pyi"
    test ! -e "$api/inputs/arrival/tsugumi/merge/TemperatureBox.pyi"
    grep -F 'TemperatureT | inputs.arrival.tsugumi.merge.t1.ReadingT | None' "$api/README.md"

    ${
      if isDarwin then
        ''test "$(nm -gUj "$card")" = _tsugumi_main''
      else
        ''test "$(nm -D --defined-only -j "$card")" = tsugumi_main''
    }
    strings -a "$card" > card.strings
    nm "$card" | c++filt > card.symbols
    if grep -Ei 'BFBS|reflection_generated|flatbuffers.*reflection|reflection::' card.strings card.symbols; then
      echo "Unexpected reflection data in main.so" >&2
      exit 1
    fi
    touch "$out"
  ''
