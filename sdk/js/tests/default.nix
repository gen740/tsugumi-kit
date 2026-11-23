{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/js> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ./interface/_inputs.fbs;
    outputs = ./interface/_outputs.fbs;
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
