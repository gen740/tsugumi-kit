{ self, ... }:
{
  perSystem =
    { pkgs, system, ... }:
    {
      checks.rAbi9 = pkgs.callPackage ../tests/r {
        kit = self.lib.${system};
      };
    };
}
