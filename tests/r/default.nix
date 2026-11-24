{ pkgs, kit }:
let
  sdk = kit.r;
  schemas = ../../tests/contract;
  common = {
    inputs = schemas + "/_inputs.fbs";
    outputs = schemas + "/_outputs.fbs";
    params = import (schemas + "/params.nix");
  };
  effects = pkgs.runCommandCC "tsugumi-r-test-effects" { buildInputs = [ pkgs.R ]; } ''
    mkdir -p "$out"
    $CXX -std=c++2c -Wall -Wextra -Werror -shared -fPIC -pthread       -I"${pkgs.R}/lib/R/include" -I"${kit.cppRuntime}/include"       -L"${pkgs.R}/lib/R/lib" -lR ${./effects.cc} -o "$out/contract_effects.so"
  '';
  fixture = pkgs.runCommand "tsugumi-r-contract-src" { } ''
    mkdir -p "$out"
    substitute ${./card/src/main.R} "$out/main.R"       --replace-fail '@EFFECTS@' "${effects}/contract_effects.so"
  '';

  card =
    params:
    sdk.rCard (
      common
      // {
        inherit params;
        src = fixture;
      }
    );
  api = params: sdk.rApi (common // { inherit params; });
  changed = common.params // {
    offset = 200;
  };
in
assert (card common.params).drvPath != (card changed).drvPath;
assert (api common.params).drvPath == (api changed).drvPath;
(import ../../nix/contract-check.nix { inherit pkgs; }) {
  name = "r";
  card = card common.params;
  secondCard = card changed;
  api = api common.params;
  runtime = kit.cppRuntime;
}
