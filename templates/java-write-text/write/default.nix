{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/java> { inherit pkgs; };
  common = {
    inherit inputs params;
    outputs = ./_outputs.fbs;
  };
in
{
  run = sdk.javaCard (common // { src = ./src; });
  api = sdk.javaApi common;
  views = sdk.views;
}
