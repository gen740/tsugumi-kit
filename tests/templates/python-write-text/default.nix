{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/python> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ../../../templates/python-write-text/write/_outputs.fbs;
  };
in
{
  run = sdk.pythonCard (common // { src = ../../../templates/python-write-text/write/src; });
  api = sdk.pythonApi common;
  views = sdk.views;
}
