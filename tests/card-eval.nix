{ pkgs }:
rec {
  inputs =
    directory:
    pkgs.lib.mapAttrs'
      (
        file: _:
        let
          name = pkgs.lib.removeSuffix ".fbs" file;
        in
        pkgs.lib.nameValuePair name (
          {
            type = pkgs.writeText file (builtins.readFile (directory + "/${file}"));
          }
          // pkgs.lib.optionalAttrs (
            !(builtins.elem name [
              "arrival"
              "draft"
            ])
          ) { exports = { }; }
        )
      )
      (
        pkgs.lib.filterAttrs (file: kind: kind == "regular" && pkgs.lib.hasSuffix ".fbs" file) (
          builtins.readDir directory
        )
      );

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
      original.run.drvPath == changed.run.drvPath && original.api.drvPath == changed.api.drvPath
    ) "${toString definition}: a parameter value changed the run or api derivation";
    original;
}
