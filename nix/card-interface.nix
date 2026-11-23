{ pkgs }:
{
  inputs,
  outputs,
  params ? { },
}:
let
  inherit (pkgs) lib;
  baseTypes = {
    bool = "bool";
    int = "long";
    float = "double";
    string = "string";
  };
  paramType =
    name: value:
    let
      kind = builtins.typeOf value;
      element = builtins.typeOf (builtins.head value);
    in
    if builtins.hasAttr kind baseTypes then
      baseTypes.${kind}
    else if
      kind == "list"
      && builtins.length value != 0
      && builtins.hasAttr element baseTypes
      && builtins.all (item: builtins.typeOf item == element) value
    then
      "[${baseTypes.${element}}]"
    else
      throw "params.nix: ${name} must be a bool, int, float, string or a non-empty homogeneous list of them";
  fields =
    assert lib.assertMsg (builtins.isAttrs params)
      "params.nix: expected an attribute set of parameter values";
    builtins.mapAttrs (
      name: value:
      if
        builtins.match "[A-Za-z0-9_.-]{1,128}" name == null
        || builtins.elem name [
          "."
          ".."
        ]
      then
        throw "params.nix: invalid parameter name ${name}"
      else
        paramType name value
    ) params;
  # Only field names and FlatBuffers types enter the derivation.
  paramsFile = pkgs.writeText "_params.fbs" ''
    namespace parameters;
    table Params {
    ${lib.concatStringsSep "\n" (
      lib.imap0 (id: name: "  ${name}:${fields.${name}} (id: ${toString id});") (
        builtins.attrNames fields
      )
    )}
    }
    root_type Params;
  '';
  outputDir = builtins.dirOf outputs;
  outputSources = lib.fileset.toSource {
    root = outputDir;
    fileset = lib.fileset.fileFilter (file: file.hasExt "fbs") outputDir;
  };
  portArgs = lib.concatMap (name: [
    name
    "${inputs.${name}.type}"
  ]) (builtins.attrNames inputs);
in
pkgs.runCommand "tsugumi-card-interface" { nativeBuildInputs = [ pkgs.python3 ]; } ''
  python3 ${../sdk/codegen/card_interface.py} "$out" \
    ${lib.escapeShellArg "${outputSources}/${baseNameOf outputs}"} \
    ${paramsFile} ${lib.escapeShellArgs portArgs}
''
