{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/java> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ../../../templates/java-write-text/write/_outputs.fbs;
  };
in
{
  run = sdk.javaCard (common // { src = ../../../templates/java-write-text/write/src; });
  api = sdk.javaApi common;
  views = sdk.views;
}
