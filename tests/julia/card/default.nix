{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/julia> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ../interface/_inputs.fbs;
    outputs = ../interface/_outputs.fbs;
  };
in
{
  run = sdk.juliaCard (
    common
    // {
      src = ./src;
    }
  );
  api = sdk.juliaApi common;
  views = sdk.views;
}
