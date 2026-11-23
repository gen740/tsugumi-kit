{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/csharp> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ../interface/_inputs.fbs;
    outputs = ../interface/_outputs.fbs;
  };
in
{
  run = sdk.csharpCard (
    common
    // {
      src = ./src;
      lock = ./packages.lock.json;
      hash = "sha256-N/TMjhwyaHux4hi7cSjpCjM9o4BmoxY+NhFx+g5tIDw=";
    }
  );
  api = sdk.csharpApi common;
  views = sdk.views;
}
