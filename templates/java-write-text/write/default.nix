{ inputs, ... }:
let
  sdk = inputs.tsugumi.javaSdk;
in
{
  run = sdk.javaCard { src = ./src; };
  views = sdk.views;
}
