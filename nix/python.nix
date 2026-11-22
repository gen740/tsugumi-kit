{ self, ... }:
{
  perSystem =
    {
      pkgs,
      system,
      ...
    }:
    {
      checks.pythonAbi9 = pkgs.callPackage ../tests/python {
        kit = self.lib.${system};
      };
    };
}
