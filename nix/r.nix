{
  perSystem =
    { pkgs, kit, ... }:
    {
      checks.rAbi = pkgs.callPackage ../tests/r {
        inherit kit;
      };
    };
}
