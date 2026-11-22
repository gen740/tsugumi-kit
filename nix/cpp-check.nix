{
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  zlib,
  cppCard,
  view,
  runtime,
}:
let
  contract = ../tests/cpp/contract;
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
    ];
  };
  definitionDir = ../tests/card;
  bound = cppCard { inherit contract definitionDir; };
  definition = import definitionDir {
    pkgs = { inherit zlib; };
    inputs.tsugumi.cppCard = bound;
  };
  card = definition.run;
  direct = cppCard {
    inherit contract definitionDir;
    src = ../tests/card/src;
    buildInputs = [ zlib ];
    cxxflags = [
      "-DABI9_TEST_CARD"
      "-lz"
    ];
  };
  checkTemplate =
    template:
    let
      definition = import template {
        inputs.tsugumi.cppCard = args: {
          inherit (args) src;
          inherit (card) views;
        };
      };
    in
    definition.run.src == template + "/src" && definition.views == card.views;
  overrides = {
    text = card.views.markdown;
    custom = card.views.image;
  };
  custom = bound {
    src = ../tests/card/src;
    views = overrides;
  };
  routes = lib.mapAttrsToList (_: view: view.tsugumiView) definition.views;
  fails = value: !(builtins.tryEval value).success;
  inherit (stdenv.hostPlatform) isDarwin;
in
assert
  builtins.functionArgs bound == {
    src = false;
    buildInputs = true;
    cxxflags = true;
    views = true;
  };
assert card.drvPath == direct.drvPath;
assert definition.views == direct.views;
assert
  builtins.attrNames definition.views == [
    "image"
    "markdown"
    "table"
    "text"
  ];
assert custom.views == card.views // overrides;
assert lib.all (
  route:
  route == {
    kind = "js";
    entry = "index.js";
  }
) routes;
assert
  (view {
    kind = "wasm";
    src = ../tests/card/src;
    entry = "main.cc";
  }).tsugumiView == {
    kind = "wasm";
    entry = "main.cc";
  };
assert fails (view {
  kind = "html";
  src = ../tests/card/src;
  entry = "main.cc";
});
assert fails (view {
  kind = "js";
  src = ../tests/card/src;
  entry = "../default.nix";
});
assert fails
  (bound {
    src = ../tests/card/src;
    views.bad = card;
  }).views.bad;
assert checkTemplate ../templates/filter-counts/filter;
assert checkTemplate ../templates/temperature-conversion/convert;
runCommandCC "tsugumi-cpp-abi9-check"
  {
    buildInputs = [ flatbuffers ];
  }
  ''
    card=${card}/lib/main.so
    tests=${tests}
    include=${runtime}/include
    for view in ${
      lib.escapeShellArgs (
        lib.mapAttrsToList (_: view: "${view}/${view.tsugumiView.entry}") definition.views
      )
    }; do
      test -s "$view"
    done

    $CC -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I"$include" -c "$tests/cpp/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$tests/legacy.c" -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -I"${card.generated}/include" -I"$include" \
      "$tests/cpp/wire.cc" fake_host.o -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake_host
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
