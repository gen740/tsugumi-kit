{
  pkgs,
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  nlohmann_json,
  ruby,
  # The kit's root entry, `import ./. { inherit pkgs; }`.
  kit,
}:
let
  inherit (kit.ruby) rubyCard;
  runtime = kit.cppRuntime;
  tests = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../fake_host.c
      ../legacy.c
      ../wire.h
    ];
  };
  fixtureEval = import ../card-eval.nix { inherit pkgs; };
  common = {
    inputs = fixtureEval.inputs (./ports);
    outputs = ./interface/_outputs.fbs;
    params = ./interface/params.nix;
  };
  definition = fixtureEval.call ./card { inherit (common) inputs; };
  card = definition.run;
  lockedCard = rubyCard (
    common
    // {
      src = ./card/src;
      lock = ./bundle/Gemfile.lock;
      hash = "sha256-5kqVDjqNFrsQddobC5KlZFUALvji0gKYs/Bbur3Mf8w=";
    }
  );
  starter = fixtureEval.call ../templates/ruby-write-text {
    inputs = fixtureEval.inputs ../templates/text-to-path;
  };
  inherit (stdenv.hostPlatform) isDarwin;
in
assert definition.views == kit.ruby.views;
runCommandCC "tsugumi-ruby-abi-check"
  {
    passthru.apis = [
      definition.api
      starter.api
    ];
    nativeBuildInputs = [ ruby ];
    buildInputs = [
      flatbuffers
      nlohmann_json
    ];
  }
  ''
    card=${card}/lib/main.so
    tests=${tests}
    include=${runtime}/include
    export HOME="$TMPDIR"
    for api in ${definition.api}/share/tsugumi/api ${starter.api}/share/tsugumi/api; do
      test -s "$api/card.rbs"
      rbs --no-collection -I "$api" validate
    done
    grep -Fq '"center" => inputs__rows__Position?' "${definition.api}/share/tsugumi/api/card.rbs"
    grep -q '^- `arrival` — `({ "type" => "Temperature"' "${definition.api}/share/tsugumi/api/README.md"
    for view in ${lib.escapeShellArgs (map (view: "${view.blob}") starter.views)}; do
      test -s "$view"
    done
    $CC -std=c11 -D_DEFAULT_SOURCE -Dmain=shared_fake_host_main \
      -Wall -Wextra -Werror -I"$include" -c "$tests/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$tests/legacy.c" -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -I"${card.generated}/include" -I"$include" -I"$tests" -I${../../sdk/cpp/src} \
      ${./wire.cc} fake_host.o ${runtime}/lib/libtsugumi_abi.a \
      -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o fake_host
    export RUBYOPT=-rtsugumi_test_missing_startup_library
    export RUBYLIB="$PWD/worker-ruby"
    export GEM_HOME="$PWD/worker-gems" GEM_PATH="$PWD/worker-gem-path"
    export BUNDLE_GEMFILE="$PWD/worker-Gemfile" BUNDLE_PATH="$PWD/worker-bundle"
    export BUNDLE_APP_CONFIG="$PWD/worker-bundle-config" BUNDLE_FROZEN=false
    export BUNDLE_FORCE_RUBY_PLATFORM=false BUNDLE_DISABLE_VERSION_CHECK=false
    export BUNDLE_VERSION=worker
    TSUGUMI_TEST_LOCKED=0 ./fake_host "$card" "$PWD/legacy.so"
    TSUGUMI_TEST_LOCKED=1 ./fake_host "${lockedCard}/lib/main.so" "$PWD/legacy.so"

    $CXX -std=c++2c -Wall -Wextra -Werror -I"${starter.run.generated}/include" -I"$include" -I"$tests" \
      ${./starter.cc} ${lib.optionalString (!isDarwin) "-ldl"} -o starter-host
    ./starter-host "${starter.run}/lib/main.so"

    for card in "$card" "${lockedCard}/lib/main.so"; do
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
    done
    touch "$out"
  ''
