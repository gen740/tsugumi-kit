{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/cpp> { inherit pkgs; };
  common = {
    inherit inputs;
    outputs = ../../../templates/filter-counts/filter/_outputs.fbs;
    params = ./params.nix;
  };
in
{
  run = sdk.cppCard (
    common
    // {
      src = pkgs.runCommand "tsugumi-filter-test-source" { } ''
        mkdir "$out"
        substitute ${../../../templates/filter-counts/filter/src/card.cc} "$out/card.cc" \
          --replace-fail 'count >= input.params.minimum' \
          'input.params.minimum <= 0 || count >= static_cast<uint64_t>(input.params.minimum)'
      '';
    }
  );
  api = sdk.cppApi common;
  views = sdk.views;
}
