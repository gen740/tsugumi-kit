{
  pkgs,
  params,
  inputs,
  schemas,
}:
let
  sdk = inputs.tsugumi.pythonSdk;
in
{
  run = sdk.pythonCard {
    inherit schemas;
    src = ./src;
    tools = pkgs.lib.optional params.withTools (
      pkgs.writeShellScriptBin "tsugumi-python-probe" "exit 42"
    );
    packages = ps: [
      ps.packaging
      ps.plotly
      ps.matplotlib
      (ps.buildPythonPackage {
        pname = "tsugumi-pth-probe";
        version = "1";
        pyproject = false;
        dontUnpack = true;
        installPhase = ''
          site="$out/${ps.python.sitePackages}"
          mkdir -p "$site/tsugumi-pth-modules"
          cat > "$site/tsugumi-pth-probe.pth" <<'PYTHON'
          tsugumi-pth-modules
          import sys, tsugumi_pth_probe; sys.path.insert(0, ""); tsugumi_pth_probe.loaded = True
          PYTHON
          echo 'loaded = False' > "$site/tsugumi-pth-modules/tsugumi_pth_probe.py"
        '';
      })
    ];
  };
  views = sdk.views;
}
