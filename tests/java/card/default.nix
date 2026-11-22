{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.javaSdk;
in
{
  run = sdk.javaCard {
    inherit schemas;
    src = ./src;
    jars = [ pkgs.testJar ];
    tools = [ pkgs.testSort ];
  };
  views = sdk.views;
}
