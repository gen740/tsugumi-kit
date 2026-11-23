{
  perSystem =
    {
      pkgs,
      kit,
      ...
    }:
    {
      checks.goAbi = pkgs.callPackage ../tests/go/check.nix {
        inherit kit;
        runtime = kit.cppRuntime;
      };
    };
}
