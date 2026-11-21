{
  config,
  lib,
  self,
  withSystem,
  ...
}:
{
  # flake-parts otherwise treats the freeform flake.lib attribute as unique.
  options.flake.lib = lib.mkOption {
    type = lib.types.lazyAttrsOf (lib.types.lazyAttrsOf lib.types.raw);
  };

  config = {
    flake.lib = lib.genAttrs config.systems (
      system:
      withSystem system (
        { pkgs, config, ... }:
        {
          csharpCard = import ../sdk/csharp/build.nix {
            inherit pkgs;
            cppRuntime = config.packages.cppRuntime;
          };
        }
      )
    );

    perSystem =
      {
        config,
        pkgs,
        system,
        ...
      }:
      {
        checks.csharpAbi9 = pkgs.callPackage ../tests/csharp/check.nix {
          inherit (self.lib.${system}) csharpCard;
          inherit (config.packages) cppRuntime;
        };
      };
  };
}
