{
  lib,
  stdenv,
  runCommandCC,
  flatbuffers,
  nlohmann_json,
  rubyCard,
  runtime,
}:
let
  contract = ./contract;
  card = rubyCard {
    src = ./card;
    inherit contract;
    sourcePrefix = "src";
  };
  lockedCard = rubyCard {
    src = ./card;
    inherit contract;
    sourcePrefix = "src";
    lock = ./bundle/Gemfile.lock;
    hash = "sha256-5kqVDjqNFrsQddobC5KlZFUALvji0gKYs/Bbur3Mf8w=";
  };
  inherit (stdenv.hostPlatform) isDarwin;
in
runCommandCC "tsugumi-ruby-abi9-check"
  {
    nativeBuildInputs = [ flatbuffers ];
    buildInputs = [
      flatbuffers
      nlohmann_json
    ];
  }
  ''
    card=${card}/lib/main.so
    tests=${../.}
    include=${runtime}/include
    for schema in _inputs _outputs _params; do
      flatc --cpp --gen-object-api --scoped-enums -o . ${contract}/"$schema.fbs"
    done
    $CC -std=c11 -D_DEFAULT_SOURCE -Dmain=shared_fake_host_main \
      -Wall -Wextra -Werror -I"$include" -c "$tests/fake_host.c"
    $CC -std=c11 -Wall -Wextra -Werror -fPIC -shared "$tests/legacy.c" -o legacy.so
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -I"$include" -I"$tests" -I${../../sdk/cpp/src} \
      ${./wire.cc} fake_host.o ${runtime}/lib/libtsugumi_abi9.a \
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
