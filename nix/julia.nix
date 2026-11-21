{
  config,
  lib,
  self,
  withSystem,
  ...
}:
{
  # flake-parts leaves custom flake outputs unique unless a module types them.
  options.flake.lib = lib.mkOption {
    type = lib.types.lazyAttrsOf (lib.types.lazyAttrsOf lib.types.raw);
  };

  config = {
    flake.lib = lib.genAttrs config.systems (
      system:
      withSystem system (
        { pkgs, config, ... }:
        {
          juliaCard = import ../sdk/julia/card.nix {
            inherit pkgs;
            runtime = config.packages.cppRuntime;
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
        checks.juliaAbi9 = pkgs.callPackage ../tests/julia/check.nix {
          juliaCard = self.lib.${system}.juliaCard;
          runtime = config.packages.cppRuntime;
        };
      };
  };
}
