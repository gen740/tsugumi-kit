{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/cpp> { inherit pkgs; };
  common = {
    inherit params;
    outputs = ./_outputs.fbs;
  };
in
{
  run = sdk.cppCard (common // { src = ./src; });
  api = sdk.cppApi common;
  views = sdk.views ++ [
    (sdk.dawnView {
      name = "clear";
      src = ./views;
      cflags = [
        "-Wall"
        "-Wextra"
        "-Werror"
      ];
    })
  ];
}
