{
  pkgs,
  kit,
  runtime,
}:
let
  schemas = ../contract;
  common = {
    inputs = schemas + "/_inputs.fbs";
    outputs = schemas + "/_outputs.fbs";
    params = import (schemas + "/params.nix");
  };
  card =
    params:
    kit.go.goCard (common // {
      inherit params;
      src = ./src;
      buildInputs = [ pkgs.zlib ];
    });
  api = params: kit.go.goApi (common // { inherit params; });
  changed = common.params // { offset = 101; };
in
assert (card common.params).drvPath != (card changed).drvPath;
assert (api common.params).drvPath == (api changed).drvPath;
(import ../../nix/contract-check.nix { inherit pkgs; }) {
  name = "go";
  card = card common.params;
  api = api common.params;
  inherit runtime;
}
