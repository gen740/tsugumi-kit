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
    kit.csharp.csharpCard (
      common
      // {
        inherit params;
        src = ./card/src;
        lock = ./card/packages.lock.json;
        hash = "sha256-N/TMjhwyaHux4hi7cSjpCjM9o4BmoxY+NhFx+g5tIDw=";
      }
    );
  api = params: kit.csharp.csharpApi (common // { inherit params; });
  changed = common.params // {
    offset = 200;
  };
in
assert (card common.params).drvPath != (card changed).drvPath;
assert (api common.params).drvPath == (api changed).drvPath;
(import ../../nix/contract-check.nix { inherit pkgs; }) {
  name = "csharp";
  card = card common.params;
  secondCard = card changed;
  api = api common.params;
  runtime = kit.cppRuntime;
}
