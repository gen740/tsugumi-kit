{ pkgs, kit }:
let
  sdk = kit.ruby;
  schemas = ../../tests/contract;
  common = {
    inputs = schemas + "/_inputs.fbs";
    outputs = schemas + "/_outputs.fbs";
    params = import (schemas + "/params.nix");
  };
  card =
    params:
    sdk.rubyCard (
      common
      // {
        inherit params;
        src = ./card/src;
        lock = ./bundle/Gemfile.lock;
        hash = "sha256-5kqVDjqNFrsQddobC5KlZFUALvji0gKYs/Bbur3Mf8w=";
      }
    );
  api = params: sdk.rubyApi (common // { inherit params; });
  changed = common.params // {
    offset = 200;
  };
in
assert (card common.params).drvPath != (card changed).drvPath;
assert (api common.params).drvPath == (api changed).drvPath;
(import ../../nix/contract-check.nix { inherit pkgs; }) {
  name = "ruby";
  card = card common.params;
  secondCard = card changed;
  api = api common.params;
  runtime = kit.cppRuntime;
}
