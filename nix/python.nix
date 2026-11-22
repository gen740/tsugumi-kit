{ self, ... }:
{
  perSystem =
    {
      pkgs,
      system,
      ...
    }:
    {
      checks.pythonAbi = pkgs.callPackage ../tests/python {
        kit = self.lib.${system};
      };
    };
}
