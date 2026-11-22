{
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
      {
        inherit (pkgs.callPackage ./cpp-card.nix { runtime = config.packages.cppRuntime; }) cppCard;
        inherit (import ./view.nix { inherit (pkgs) lib runCommand; }) view;
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
