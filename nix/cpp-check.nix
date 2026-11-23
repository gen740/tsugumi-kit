{ pkgs, kit }:
let
  sdk = kit.cpp;
  common = {
    inputs = ../tests/contract/_inputs.fbs;
    outputs = ../tests/contract/_outputs.fbs;
    params = import ../tests/contract/params.nix;
  };
  variant = common // {
    params = common.params // {
      offset = 200;
    };
  };
  build = args: sdk.cppCard (args // { src = ../tests/card/src; });
in
assert (sdk.cppApi common).drvPath == (sdk.cppApi variant).drvPath;
assert (build common).drvPath != (build variant).drvPath;
import ./contract-check.nix { inherit pkgs; } {
  name = "cpp";
  card = build common;
  runtime = kit.cppRuntime;
  api = sdk.cppApi common;
}
