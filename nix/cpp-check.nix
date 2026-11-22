{
  pkgs,
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  coreutils,
  writeShellScriptBin,
  zlib,
  # The kit's root entry, `import ./. { inherit pkgs; }`.
  kit,
}:
let
  inherit (kit) cppSdk;
  inherit (cppSdk) cppCard;
  runtime = kit.cppRuntime;
  schemas = ../tests/cpp/schemas;
  tests = lib.fileset.toSource {
    root = ../tests;
    fileset = lib.fileset.unions [
      ../tests/fake_host.c
      ../tests/legacy.c
      ../tests/wire.cc
      ../tests/wire.h
      ../tests/cpp/cases.h
      ../tests/cpp/fake_host.c
      ../tests/cpp/wire.cc
      ../tests/cpp/template.cc
    ];
  };
  testTool = writeShellScriptBin "tsugumi-test-tool" "exit 42";
  cardArgs = {
    pkgs = pkgs // {
      inherit testTool;
    };
    inputs.tsugumi = kit;
    params = { };
    inherit schemas;
  };
  definition = import ../tests/card cardArgs;
  card = definition.run;
  withoutTools = cppCard {
    inherit schemas;
    src = ../tests/card/src;
    buildInputs = [ zlib ];
    cxxflags = [
      "-DABI_TEST_CARD"
      "-lz"
    ];
  };
  template =
    definitionDir: schemas:
    import definitionDir {
      inherit pkgs schemas;
      inputs.tsugumi = kit;
      params = { };
    };
  filter = template ../templates/filter-counts/filter ../tests/templates/filter-counts;
  convert = template ../templates/temperature-conversion/convert ../tests/templates/temperature-conversion;
  convertUnwired = template ../templates/temperature-conversion/convert (
    pkgs.runCommand "tsugumi-unwired-converter-schemas" { } ''
      mkdir "$out"
      cp ${../tests/templates/temperature-conversion}/_{outputs,params}.fbs "$out/"
      printf 'namespace inputs; table Inputs {} root_type Inputs;\n' > "$out/_inputs.fbs"
    ''
  );
  inherit (stdenv.hostPlatform) isDarwin;
in
assert definition.views == cppSdk.views;
assert !(card ? views);
assert
  map (view: view.name) definition.views == [
    "text"
    "markdown"
    "table"
    "image"
    "line-chart"
    "points"
  ];
assert lib.all (view: view.type == "html" && view ? blob) definition.views;
assert lib.all
  (
    sdk:
    lib.filter (view: view.name == "line-chart" || view.name == "points") sdk.views
    == lib.filter (view: view.name == "line-chart" || view.name == "points") cppSdk.views
  )
  (
    with kit;
    [
      pythonSdk
      goSdk
      rustSdk
      csharpSdk
      juliaSdk
      rSdk
      rubySdk
    ]
  );
assert filter.views == cppSdk.views && convert.views == cppSdk.views;
runCommandCC "tsugumi-cpp-abi-check"
  {
    buildInputs = [ flatbuffers ];
    passthru.apiCards = [
      card
      withoutTools
      filter.run
      convert.run
      convertUnwired.run
    ];
  }
  ''
    card=${card}/lib/main.so
    tests=${tests}
    include=${runtime}/include
    for view in ${lib.escapeShellArgs (map (view: "${view.blob}") definition.views)}; do
      test -s "$view"
    done

    $CC -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I"$include" -c "$tests/cpp/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$tests/legacy.c" -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -I"${card.generated}/include" -I"$include" \
      "$tests/cpp/wire.cc" fake_host.o -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake_host
    test -L "${card}/bin/sort"
    test ! -e "${card}/bin/sh"
    test ! -e "${withoutTools}/bin"
    env -i ./fake_host --tools "$card" "${card}/bin"
    env -i ./fake_host --tools "${withoutTools}/lib/main.so" ""
    ./fake_host "$card" "$PWD/legacy.so"

    $CXX -std=c++2c -Wall -Wextra -Werror -I"${filter.run.generated}/include" -I"$include" \
      -DFILTER "$tests/cpp/template.cc" ${lib.optionalString (!isDarwin) "-ldl"} -o filter-host
    ./filter-host "${filter.run}/lib/main.so"
    $CXX -std=c++2c -Wall -Wextra -Werror -I"${convert.run.generated}/include" -I"$include" \
      "$tests/cpp/template.cc" ${lib.optionalString (!isDarwin) "-ldl"} -o convert-host
    ./convert-host "${convert.run}/lib/main.so"
    ./convert-host "${convertUnwired.run}/lib/main.so" --unwired


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
