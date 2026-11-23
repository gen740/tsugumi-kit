{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/go> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ../../../templates/go-normalize-text/normalize/_outputs.fbs;
  };
in
{
  run = sdk.goCard (common // { src = ../../../templates/go-normalize-text/normalize/src; });
  api = sdk.goApi common;
  views = sdk.views;
}
