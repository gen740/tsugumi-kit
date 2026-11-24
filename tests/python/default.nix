{ pkgs, kit }:
let
  sdk = kit.python;
  common = {
    inputs = ../contract/_inputs.fbs;
    outputs = ../contract/_outputs.fbs;
    params = import ../contract/params.nix;
  };
  variant = common // {
    params = common.params // {
      offset = 200;
    };
  };
  build = args: sdk.pythonCard (args // { src = ./src; });
in
assert (sdk.pythonApi common).drvPath == (sdk.pythonApi variant).drvPath;
assert (build common).drvPath != (build variant).drvPath;
import ../../nix/contract-check.nix { inherit pkgs; } {
  name = "python";
  card = build common;
  secondCard = build variant;
  runtime = kit.cppRuntime;
  api = sdk.pythonApi common;
}
