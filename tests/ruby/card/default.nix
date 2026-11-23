{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/ruby> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ../interface/_outputs.fbs;
    params = ../interface/params.nix;
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
