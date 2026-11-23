{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/julia> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ../../../templates/julia-write-text/write/_outputs.fbs;
  };
in
{
  run = sdk.juliaCard (common // { src = ../../../templates/julia-write-text/write/src; });
  api = sdk.juliaApi common;
  views = sdk.views;
}
