{
  config,
  lib,
  self,
  withSystem,
  ...
}:
{
  # Allow the per-system builder sets supplied by the language modules to merge.
  options.flake.lib = lib.mkOption {
    type = lib.types.lazyAttrsOf (lib.types.lazyAttrsOf lib.types.raw);
  };

  config.flake.lib = lib.genAttrs config.systems (
    system:
    withSystem system (
      { pkgs, config, ... }:
      {
        rCard = pkgs.callPackage ../sdk/r { runtime = config.packages.cppRuntime; };
      }
    )
  );

  config.perSystem =
    {
      config,
      pkgs,
      system,
      ...
    }:
    {
      checks.rAbi9 = pkgs.callPackage ../tests/r {
        rCard = self.lib.${system}.rCard;
        runtime = config.packages.cppRuntime;
      };
    };
}
