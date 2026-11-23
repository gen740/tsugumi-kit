{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/ruby> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ../../../templates/ruby-write-text/write/_outputs.fbs;
  };
in
{
  run = sdk.rubyCard (common // { src = ../../../templates/ruby-write-text/write/src; });
  api = sdk.rubyApi common;
  views = sdk.views;
}
