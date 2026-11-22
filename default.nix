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
  views = pkgs.callPackage ./sdk/views { };
  builders = {
    inherit (pkgs.callPackage ./nix/cpp-card.nix { inherit runtime; }) cppCard;
    pythonCard = pkgs.callPackage ./sdk/python { inherit runtime; };
    inherit (import ./nix/go-card.nix { inherit pkgs runtime; }) goCard goCardSdk;
    rustCard = import ./sdk/rust {
      inherit pkgs;
      abi = ./abi/9;
    };
    csharpCard = import ./sdk/csharp/build.nix { inherit pkgs cppRuntime; };
    juliaCard = import ./sdk/julia/card.nix { inherit pkgs runtime; };
    rCard = pkgs.callPackage ./sdk/r { inherit runtime; };
    rubyCard = import ./nix/ruby-card.nix { inherit pkgs runtime; };
  };
  sdks = with builders; {
    cppSdk = {
      inherit cppCard;
      views = views.cpp;
    };
    pythonSdk = {
      inherit pythonCard;
      views = views.python;
    };
    goSdk = {
      inherit goCard goCardSdk;
      views = [ ];
    };
    rustSdk = {
      inherit rustCard;
      views = [ ];
    };
    csharpSdk = {
      inherit csharpCard;
      views = [ ];
    };
    juliaSdk = {
      inherit juliaCard;
      views = [ ];
    };
    rSdk = {
      inherit rCard;
      views = [ ];
    };
    rubySdk = {
      inherit rubyCard;
      views = [ ];
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
