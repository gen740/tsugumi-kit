{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/js> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ./interface/_outputs.fbs;
    params = ./interface/params.nix;
  };
in
{
  run = sdk.jsCard (
    common
    // {
      src = ./src;
      tools = [ pkgs.coreutils ];
    }
  );
  api = sdk.jsApi common;
  views = sdk.views;
}
