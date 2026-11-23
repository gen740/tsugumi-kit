{
  perSystem =
    {
      pkgs,
      kit,
      ...
    }:
    {
      checks.pythonAbi = pkgs.callPackage ../tests/python {
        inherit kit;
      };
    };
}
