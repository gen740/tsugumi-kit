{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/r> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ../../../templates/r-write-text/write/_outputs.fbs;
  };
in
{
  run = sdk.rCard (common // { src = ../../../templates/r-write-text/write/src; });
  api = sdk.rApi common;
  views = sdk.views;
}
