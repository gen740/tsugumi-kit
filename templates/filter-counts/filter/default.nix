{ inputs, ... }:
let
  sdk = inputs.tsugumi.cppSdk;
in
{
  run = sdk.cppCard { src = ./src; };
  views = sdk.views;
}
