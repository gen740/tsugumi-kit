{ pkgs, kit }:
let
  sdk = kit.js;
  schemas = ../tests/contract;
  common = {
    inputs = schemas + "/_inputs.fbs";
    outputs = schemas + "/_outputs.fbs";
    params = import (schemas + "/params.nix");
  };
  card =
    params:
    sdk.jsCard (
      common
      // {
        inherit params;
        src = ../sdk/js/tests/src;
      }
    );
  api = params: sdk.jsApi (common // { inherit params; });
  changed = common.params // {
    offset = 101;
  };
in
assert (card common.params).drvPath != (card changed).drvPath;
assert (api common.params).drvPath == (api changed).drvPath;
(import ./contract-check.nix { inherit pkgs; }) {
  name = "js";
  card = card common.params;
  api = api common.params;
  runtime = kit.cppRuntime;
}
