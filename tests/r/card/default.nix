{ params, inputs }:
let
  pkgs = import <nixpkgs> { };
  sdk = import <tsugumi-kit/sdk/r> { inherit pkgs; };
  common = {
    inherit params;
    inputs = ../interface/_inputs.fbs;
    outputs = ../interface/_outputs.fbs;
  };
  runtime = (import <tsugumi-kit> { inherit pkgs; }).cppRuntime;
  # The fixture's native extension calls the SDK's registered C effects.
  src =
    pkgs.runCommandCC "tsugumi-r-test-source"
      {
        buildInputs = [
          pkgs.flatbuffers
          pkgs.R
        ];
      }
      ''
        mkdir -p "$out"
        cp ${../../wire.h} wire.h
        $CXX -std=c++2c -Wall -Wextra -Werror -fPIC -shared -pthread \
          -I"${runtime}/include" -I"${pkgs.R}/lib/R/include" -I. \
          ${../effects.cc} -L"${pkgs.R}/lib/R/lib" -lR -o "$out/effects.so"
        substitute "${./src/main.R}" "$out/main.R" \
          --replace-fail @native_effects@ "$out/effects.so"
      '';
in
{
  run = sdk.rCard (common // { inherit src; });
  api = sdk.rApi common;
  views = sdk.views;
}
