# The SDK set built with the caller's nixpkgs. The Tsugumi core imports this
# with the project's pin; the flake's `lib.${system}` is this set with the
# kit's own pin.
{ pkgs }:
let
  portCodegen = pkgs.callPackage ./nix/port-codegen.nix { };
  cppRuntime = pkgs.callPackage ./nix/cpp-runtime.nix {
    abiHeaders = pkgs.callPackage ./nix/abi-headers.nix { };
  };
  runtime = cppRuntime;
  ipaexGothic = pkgs.callPackage ./nix/ipaex-gothic.nix { };
  views = pkgs.callPackage ./sdk/views { };
  builders = {
    inherit (pkgs.callPackage ./nix/cpp-card.nix { inherit runtime portCodegen; }) cppCard;
    pythonCard = pkgs.callPackage ./sdk/python { inherit runtime ipaexGothic portCodegen; };
    javaCard = pkgs.callPackage ./nix/java-card.nix { inherit runtime portCodegen; };
    inherit (import ./nix/go-card.nix { inherit pkgs runtime; }) goCard goCardSdk;
    rustCard = import ./sdk/rust {
      inherit pkgs;
      abi = ./abi;
    };
    csharpCard = import ./sdk/csharp/build.nix { inherit pkgs cppRuntime portCodegen; };
    juliaCard = import ./sdk/julia/card.nix { inherit pkgs runtime portCodegen; };
    rCard = pkgs.callPackage ./sdk/r { inherit runtime ipaexGothic portCodegen; };
    rubyCard = import ./nix/ruby-card.nix { inherit pkgs runtime portCodegen; };
    jsCard = pkgs.callPackage ./nix/js-card.nix {
      inherit runtime portCodegen;
      libnode = pkgs.callPackage ./nix/js-libnode.nix { };
    };
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
      views = views.common;
    };
    rustSdk = {
      inherit rustCard;
      views = views.common;
    };
    csharpSdk = {
      inherit csharpCard;
      views = views.common;
    };
    juliaSdk = {
      inherit juliaCard;
      views = views.common;
    };
    rSdk = {
      inherit rCard;
      views = views.common;
    };
    jsSdk = {
      inherit jsCard;
      views = views.common;
    };
    rubySdk = {
      inherit rubyCard;
      views = views.common;
    };
  };
in
builders
// sdks
// {
  inherit cppRuntime portCodegen;
}
