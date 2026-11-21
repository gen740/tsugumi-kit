{
  config,
  lib,
  self,
  withSystem,
  ...
}:
{
  options.flake.lib = lib.mkOption {
    type = lib.types.lazyAttrsOf (lib.types.lazyAttrsOf lib.types.raw);
    description = "Card builders by system and language.";
  };

  config = {
    flake.lib = lib.genAttrs config.systems (
      system:
      withSystem system (
        { pkgs, config, ... }:
        {
          pythonCard = pkgs.callPackage ../sdk/python { runtime = config.packages.cppRuntime; };
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
        checks.pythonAbi9 = pkgs.callPackage ../tests/python {
          pythonCard = self.lib.${system}.pythonCard;
          runtime = config.packages.cppRuntime;
        };
      };
  };
}
