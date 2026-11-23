{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/cpp> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ../../../templates/temperature-conversion/convert/_outputs.fbs;
  };
in
{
  run = sdk.cppCard (common // { src = ../../../templates/temperature-conversion/convert/src; });
  api = sdk.cppApi common;
  views = sdk.views;
}
