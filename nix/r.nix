{ self, ... }:
{
  perSystem =
    { pkgs, system, ... }:
    {
      checks.rAbi = pkgs.callPackage ../tests/r {
        kit = self.lib.${system};
      };
    };
}
