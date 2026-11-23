{ pkgs }:
{
  inputs ? null,
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
  paramTypes = pkgs.writeText "tsugumi-param-types.json" (builtins.toJSON fields);
  schemaPath =
    path:
    let
      root = builtins.dirOf path;
      sources =
        if !builtins.isPath path then
          null
        else
          lib.fileset.toSource {
            inherit root;
            fileset = lib.fileset.fileFilter (
              file:
              file.hasExt "fbs"
              && !(builtins.elem file.name [
                "_inputs.fbs"
                "_outputs.fbs"
              ])
            ) root;
          };
      # Keep both catalog roots out of the fileset so that an Input Card
      # consumes only its own output catalog.
    in
    pkgs.runCommand "tsugumi-schema-source" { } ''
      mkdir -p "$out"
      ${lib.optionalString (sources != null) ''cp -R "${sources}/." "$out/"''}
      cp ${lib.escapeShellArg "${path}"} "$out/${baseNameOf path}"
    '';
in
pkgs.runCommand "tsugumi-card-interface" { nativeBuildInputs = [ pkgs.python3 ]; } ''
  python3 ${../sdk/codegen/card_interface.py} "$out" \
    ${
      if inputs == null then "-" else lib.escapeShellArg "${schemaPath inputs}/${baseNameOf inputs}"
    } \
    ${lib.escapeShellArg "${schemaPath outputs}/${baseNameOf outputs}"} \
    ${paramTypes} ${../abi}
''
