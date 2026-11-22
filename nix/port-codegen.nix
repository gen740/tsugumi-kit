{ python3, runCommand }:
python3.pkgs.toPythonModule (
  runCommand "tsugumi-port-codegen" { } ''
    install -Dm644 ${../sdk/codegen/tsugumi_ports.py} \
      "$out/${python3.sitePackages}/tsugumi_ports.py"
  ''
)
