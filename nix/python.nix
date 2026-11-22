{ self, ... }:
{
  perSystem =
    {
      config,
      pkgs,
      system,
      ...
    }:
    {
      checks.pythonAbi9 = pkgs.callPackage ../tests/python {
        inherit (self.lib.${system}) pythonSdk;
        runtime = config.packages.cppRuntime;
      };
    };
}
