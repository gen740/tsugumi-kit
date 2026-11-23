{ pkgs, kit }:
let
  schemas = ../contract;
  common = {
    inputs = schemas + "/_inputs.fbs";
    outputs = schemas + "/_outputs.fbs";
    params = import (schemas + "/params.nix");
  };
  card =
    params:
    kit.rust.rustCard (
      common
      // {
        inherit params;
        src = ./card;
        buildInputs = [ pkgs.zlib ];
      }
    );
  api = params: kit.rust.rustApi (common // { inherit params; });
  changed = common.params // {
    offset = 101;
  };
in
assert (card common.params).drvPath != (card changed).drvPath;
assert (api common.params).drvPath == (api changed).drvPath;
(import ../../nix/contract-check.nix { inherit pkgs; }) {
  name = "rust";
  card = card common.params;
  api = api common.params;
  runtime = kit.cppRuntime;
}
