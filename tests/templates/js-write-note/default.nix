{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/js> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ../../../templates/js-write-note/note/_outputs.fbs;
    params = ./params.nix;
  };
in
{
  run = sdk.jsCard (common // { src = ../../../templates/js-write-note/note/src; });
  api = sdk.jsApi common;
  views = sdk.views;
}
