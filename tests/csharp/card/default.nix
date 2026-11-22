{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.csharpSdk;
in
{
  run = sdk.csharpCard {
    inherit schemas;
    src = ./src;
    lock = ./packages.lock.json;
    hash = "sha256-N/TMjhwyaHux4hi7cSjpCjM9o4BmoxY+NhFx+g5tIDw=";
  };
  views = sdk.views;
}
