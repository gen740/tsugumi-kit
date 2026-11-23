{
  pkgs,
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  zlib,
  # The kit's root entry, `import ./. { inherit pkgs; }`.
  kit,
}:
let
  sdk = kit.cpp;
  runtime = kit.cppRuntime;
  fixtureEval = import ../tests/card-eval.nix { inherit pkgs; };
  inputs = fixtureEval.inputs ../tests/cpp/ports;
  params = import ../tests/cpp/interface/params.nix;
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
  definition = fixtureEval.callWithParamCheck ../tests/card { inherit inputs params; } {
    mode = 1;
    labels = [ "changed" ];
  };
  card = definition.run;
  withoutTools = sdk.cppCard {
    inherit inputs params;
    outputs = ../tests/cpp/interface/_outputs.fbs;
    src = ../tests/card/src;
    buildInputs = [ zlib ];
    inherit (params) cxxflags;
  };
  filter = fixtureEval.call ../templates/filter-counts/filter {
    inputs = fixtureEval.inputs ../tests/templates/filter-counts;
  };
  convert = fixtureEval.call ../templates/temperature-conversion/convert {
    inputs = fixtureEval.inputs ../tests/templates/temperature-conversion;
  };
  convertUnwired = fixtureEval.call ../templates/temperature-conversion/convert { };
  inherit (stdenv.hostPlatform) isDarwin;
in
assert definition.views == sdk.views;
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
    languageSdk:
    lib.filter (view: view.name == "line-chart" || view.name == "points") languageSdk.views
    == lib.filter (view: view.name == "line-chart" || view.name == "points") sdk.views
  )
  (
    with kit;
    [
      python
      go
      rust
      csharp
      julia
      r
      ruby
    ]
  );
assert filter.views == sdk.views && convert.views == sdk.views;
runCommandCC "tsugumi-cpp-abi-check"
  {
    buildInputs = [ flatbuffers ];
    passthru.apis = [
      definition.api
      filter.api
      convert.api
      convertUnwired.api
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
