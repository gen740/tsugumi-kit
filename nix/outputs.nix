{
  perSystem =
    { pkgs, kit, ... }:
    {
      _module.args.kit = import ../. { inherit pkgs; };
      formatter = pkgs.nixfmt;
      packages.cppRuntime = kit.cppRuntime;
    };
}
