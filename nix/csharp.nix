{ self, ... }:
{
  perSystem =
    { pkgs, system, ... }:
    {
      checks.csharpAbi9 = pkgs.callPackage ../tests/csharp/check.nix {
        kit = self.lib.${system};
      };
    };
}
