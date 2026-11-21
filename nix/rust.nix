{
  config,
  lib,
  self,
  withSystem,
  ...
}:
{
  # SDK modules contribute independent builder functions for each system.
  options.flake.lib = lib.mkOption {
    type = lib.types.lazyAttrsOf (lib.types.lazyAttrsOf lib.types.raw);
  };

  config = {
    flake.lib = lib.genAttrs config.systems (
      system:
      withSystem system (
        { pkgs, ... }:
        {
          rustCard = import ../sdk/rust {
            inherit pkgs;
            abi = ../abi/9;
          };
        }
      )
    );

    perSystem =
      { pkgs, system, ... }:
      {
        checks.rustAbi9 = pkgs.callPackage ../tests/rust {
          rustCard = self.lib.${system}.rustCard;
        };
      };
  };
}
