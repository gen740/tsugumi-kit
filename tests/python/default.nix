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
  # NumPy must be present when the shared FlatBuffers module is initialized.
  bundled = common // {
    packages = ps: [
      ps.numpy
      ps.astropy
    ];
  };
  build = args: sdk.pythonCard (args // { src = ./src; });
in
assert (sdk.pythonApi common).drvPath == (sdk.pythonApi variant).drvPath;
assert (build common).drvPath != (build variant).drvPath;
import ../../nix/contract-check.nix { inherit pkgs; } {
  name = "python";
  card = build bundled;
  secondCard = build variant;
  runtime = kit.cppRuntime;
  api = sdk.pythonApi common;
}
