{ inputs, ... }:
let
  sdk = inputs.tsugumi.jsSdk;
in
{
  run = sdk.jsCard { src = ./src; };
  views = sdk.views;
}
