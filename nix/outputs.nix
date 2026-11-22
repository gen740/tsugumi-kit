{
  self,
  config,
  lib,
  withSystem,
  ...
}:
{
  flake.lib = lib.genAttrs config.systems (
    system:
    withSystem system (
      { pkgs, config, ... }:
      let
        kit = self.lib.${system};
        bundled = pkgs.callPackage ../sdk/views { };
      in
      {
        inherit (pkgs.callPackage ./cpp-card.nix { runtime = config.packages.cppRuntime; }) cppCard;
        cppSdk = {
          inherit (kit) cppCard;
          views = bundled.cpp;
        };
        pythonSdk = {
          inherit (kit) pythonCard;
          views = bundled.python;
        };
        goSdk = {
          inherit (kit) goCard goCardSdk;
          views = [ ];
        };
        rustSdk = {
          inherit (kit) rustCard;
          views = [ ];
        };
        csharpSdk = {
          inherit (kit) csharpCard;
          views = [ ];
        };
        juliaSdk = {
          inherit (kit) juliaCard;
          views = [ ];
        };
        rSdk = {
          inherit (kit) rCard;
          views = [ ];
        };
        rubySdk = {
          inherit (kit) rubyCard;
          views = [ ];
        };
      }
    )
  );

  perSystem =
    { pkgs, ... }:
    {
      formatter = pkgs.nixfmt;
      packages.cppRuntime = pkgs.callPackage ./cpp-runtime.nix {
        abiHeaders = pkgs.callPackage ./abi-headers.nix { };
      };
    };
}
