{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/ruby> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ../interface/_inputs.fbs;
    outputs = ../interface/_outputs.fbs;
  };
in
{
  run = sdk.rubyCard (
    common
    // {
      src = ./src;
    }
  );
  api = sdk.rubyApi common;
  views = sdk.views;
}
