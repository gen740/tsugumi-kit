{
  lib,
  runCommand,
  fetchurl,
  esbuild,
}:
let
  commonmark = fetchurl {
    url = "https://registry.npmjs.org/commonmark/-/commonmark-0.31.2.tgz";
    hash = "sha256-2jtA61oGgktGGO1aKWS+w/TmqV/T3ZBHFfkpPHd4KPE=";
  };

  plotly = fetchurl {
    url = "https://registry.npmjs.org/plotly.js-dist-min/-/plotly.js-dist-min-3.6.0.tgz";
    hash = "sha256-gyDoll/Sr01jx75fvMFPy3lRVTU6kSIrzy9tgYgICtk=";
  };

  bundle =
    name:
    runCommand "tsugumi-python-view-${name}" { nativeBuildInputs = [ esbuild ]; } ''
      mkdir -p "$out"
      cp ${./.}/{${name},data}.js .
      ${lib.optionalString (name == "markdown") ''
        tar -xOf ${commonmark} package/dist/commonmark.js > commonmark.cjs
        tar -xOf ${commonmark} package/LICENSE > "$out/LICENSE"
      ''}
      ${lib.optionalString (name == "plotly") ''
        tar -xOf ${plotly} package/plotly.min.js > plotly.cjs
        tar -xOf ${plotly} package/LICENSE > "$out/LICENSE"
      ''}
      esbuild ${name}.js --bundle --format=esm --platform=browser --target=es2022 \
        --minify --outfile="$out/index.js"
    '';
in
lib.genAttrs [
  "text"
  "markdown"
  "table"
  "image"
  "plotly"
] bundle
