{ python3, runCommand }:
python3.pkgs.toPythonModule (
  runCommand "tsugumi-port-codegen" { } ''
    mkdir -p "$out/${python3.sitePackages}"
    cp ${../sdk/codegen/tsugumi_ports.py} "$out/${python3.sitePackages}/tsugumi_ports.py"
    cp ${../sdk/codegen/card_interface.py} "$out/${python3.sitePackages}/card_interface.py"
  ''
)
