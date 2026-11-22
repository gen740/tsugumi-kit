{ inputs, ... }:
let
  sdk = inputs.tsugumi.rustSdk;
in
{
  run = sdk.rustCard {
    src = ./src;
    sourcePrefix = "src";
  };
  views = sdk.views ++ builtins.filter (view: view.name == "text") inputs.tsugumi.cppSdk.views;
}
