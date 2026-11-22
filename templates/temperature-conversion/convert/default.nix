{ inputs, ... }:
rec {
  run = inputs.tsugumi.cppCard { src = ./src; };
  inherit (run) views;
}
