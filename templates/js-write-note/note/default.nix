{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/js> { inherit pkgs; };
  common = {
    inherit inputs params;
    outputs = ./_outputs.fbs;
  };
in
{
  run = sdk.jsCard (common // { src = ./src; });
  api = sdk.jsApi common;
  views = sdk.views;
}
