{ pkgs }:
{
  name,
  card,
  runtime,
  api,
}:
let
  fixtures = import ./card-interface.nix { inherit pkgs; } {
    inputs = ../tests/contract/_inputs.fbs;
    outputs = ../tests/contract/_outputs.fbs;
  };
in
pkgs.runCommandCC "tsugumi-${name}-contract-check"
  {
    nativeBuildInputs = [ pkgs.flatbuffers ];
    buildInputs = [ pkgs.flatbuffers ];
    passthru.apis = [ api ];
  }
  ''
    fixtures=${fixtures}
    tests=${
      pkgs.lib.fileset.toSource {
        root = ../tests;
        fileset = pkgs.lib.fileset.unions [
          ../tests/fake_host.c
          ../tests/legacy.c
          ../tests/wire.h
          ../tests/wire.cc
        ];
      }
    }
    include=${runtime}/include
    printf '#include "tsugumi/protocol/artifact_generated.h"\n' > artifact_generated.h
    printf '#include "tsugumi/protocol/card_generated.h"\n' > card_generated.h
    flatc --cpp --gen-object-api --scoped-enums -I "$fixtures" \
      "$fixtures/_inputs.fbs" "$fixtures/_outputs.fbs"
    for file in fake_host legacy; do
      $CC -std=c11 -Wall -Wextra -Werror -I"$include" -c "$tests/$file.c"
    done
    $CXX -std=c++2c -Wall -Wextra -Werror -I. -I"$include" \
      "$tests/wire.cc" fake_host.o legacy.o -pthread -ldl -o fake-host
    mkdir work
    (cd work && HOME="$PWD" ../fake-host "${card}/lib/main.so")
    test -s "${api}/share/tsugumi/api/README.md"
    touch "$out"
  ''
