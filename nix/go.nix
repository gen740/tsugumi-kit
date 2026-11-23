{
  perSystem =
    {
      pkgs,
      config,
      kit,
      ...
    }:
    {
      checks.goAbi = pkgs.callPackage ../tests/go/check.nix {
        inherit kit;
        runtime = config.packages.cppRuntime;
      };
    };
}
