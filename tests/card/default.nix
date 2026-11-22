{ inputs, pkgs, ... }:
rec {
  run = inputs.tsugumi.cppCard {
    src = ./src;
    buildInputs = [ pkgs.zlib ];
    cxxflags = [
      "-DABI9_TEST_CARD"
      "-lz"
    ];
  };
  inherit (run) views;
}
