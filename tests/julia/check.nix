{ pkgs, kit }:
let
  sdk = kit.julia;
  schemas = ../../tests/contract;
  common = {
    inputs = schemas + "/_inputs.fbs";
    outputs = schemas + "/_outputs.fbs";
    params = import (schemas + "/params.nix");
  };
  card =
    params:
    sdk.juliaCard (
      common
      // {
        inherit params;
        src = ./card/src;
      }
    );
  api = params: sdk.juliaApi (common // { inherit params; });
  changed = common.params // {
    offset = 200;
  };
in
assert (card common.params).drvPath != (card changed).drvPath;
assert (api common.params).drvPath == (api changed).drvPath;
(import ../../nix/contract-check.nix { inherit pkgs; }) {
  name = "julia";
  card = card common.params;
  secondCard = card changed;
  api = api common.params;
  runtime = kit.cppRuntime;
}
