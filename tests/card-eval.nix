{ pkgs }:
rec {
  call =
    definition:
    let
      paramsFile = definition + "/params.nix";
    in
    {
      params ? (if builtins.pathExists paramsFile then import paramsFile else { }),
      inputs ? { },
    }:
    builtins.scopedImport {
      __nixPath = [
        {
          prefix = "nixpkgs";
          path = pkgs.path;
        }
        {
          prefix = "tsugumi-kit";
          path = ../.;
        }
      ];
      # In pure or cross-system flake evaluation, reproduce the target pkgs
      # that the core supplies by setting builtins.currentSystem.
      import =
        path:
        if path == pkgs.path then
          args: builtins.import path ({ system = pkgs.stdenv.hostPlatform.system; } // args)
        else
          builtins.import path;
    } definition { inherit params inputs; };

  callWithParamCheck =
    definition: args: changes:
    let
      original = call definition args;
      changed = call definition (args // { params = args.params // changes; });
    in
    assert pkgs.lib.assertMsg (
      original.run.drvPath != changed.run.drvPath && original.api.drvPath == changed.api.drvPath
    ) "${toString definition}: parameter values must affect run while preserving api";
    original;
}
