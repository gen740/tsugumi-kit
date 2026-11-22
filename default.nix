# The SDK set built with the caller's nixpkgs. The Tsugumi core imports this
# with the project's pin; the flake's `lib.${system}` is this set with the
# kit's own pin.
{ pkgs }:
let
  inherit (pkgs) lib;
  cppRuntime = pkgs.callPackage ./nix/cpp-runtime.nix {
    abiHeaders = pkgs.callPackage ./nix/abi-headers.nix { };
  };
  runtime = cppRuntime;
  ipaexGothic = pkgs.callPackage ./nix/ipaex-gothic.nix { };
  views = pkgs.callPackage ./sdk/views { };
  builders = {
    inherit (pkgs.callPackage ./nix/cpp-card.nix { inherit runtime; }) cppCard;
    pythonCard = pkgs.callPackage ./sdk/python { inherit runtime ipaexGothic; };
    javaCard = pkgs.callPackage ./nix/java-card.nix { inherit runtime; };
    inherit (import ./nix/go-card.nix { inherit pkgs runtime; }) goCard goCardSdk;
    rustCard = import ./sdk/rust {
      inherit pkgs;
      abi = ./abi/9;
    };
    csharpCard = import ./sdk/csharp/build.nix { inherit pkgs cppRuntime; };
    juliaCard = import ./sdk/julia/card.nix { inherit pkgs runtime; };
    rCard = pkgs.callPackage ./sdk/r { inherit runtime ipaexGothic; };
    rubyCard = import ./nix/ruby-card.nix { inherit pkgs runtime; };
  };
  sdks = with builders; {
    cppSdk = {
      inherit cppCard;
      views = views.common;
    };
    pythonSdk = {
      inherit pythonCard;
      views = views.plotlyViews;
    };
    javaSdk = {
      inherit javaCard;
      views = views.common;
    };
    goSdk = {
      inherit goCard goCardSdk;
      views = views.charts;
    };
    rustSdk = {
      inherit rustCard;
      views = views.charts;
    };
    csharpSdk = {
      inherit csharpCard;
      views = views.charts;
    };
    juliaSdk = {
      inherit juliaCard;
      views = views.charts;
    };
    rSdk = {
      inherit rCard;
      views = views.charts;
    };
    rubySdk = {
      inherit rubyCard;
      views = views.charts;
    };
  };
  # A builder whose contract the core supplies; the Card passes everything else.
  withContract =
    contract: build: args:
    build (args // { inherit contract; });
in
builders
// sdks
// {
  inherit cppRuntime;

  # The SDKs as the core injects them into a Card's default.nix as
  # `inputs.tsugumi`: every builder is bound to that Card's contract, and
  # cppCard also to its definition directory for source paths.
  bind =
    { contract, definitionDir }:
    lib.mapAttrs (
      _: sdk:
      lib.mapAttrs (
        name: value:
        if name == "views" then
          value
        else if name == "cppCard" then
          value { inherit contract definitionDir; }
        else
          withContract contract value
      ) sdk
    ) sdks;
}
