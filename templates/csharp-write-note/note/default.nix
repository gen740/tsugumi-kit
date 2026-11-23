{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/csharp> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ./_outputs.fbs;
    params = ./params.nix;
  };
in
{
  run = sdk.csharpCard (common // { src = ./src; });
  api = sdk.csharpApi common;
  views = sdk.views;
}
