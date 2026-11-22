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

  bundle = name: {
    inherit name;
    type = "html";
    blob = runCommand "tsugumi-view-${name}.html" { nativeBuildInputs = [ esbuild ]; } ''
      cp ${./.}/{${name},data,chart}.js .
      ${lib.optionalString (name == "markdown") ''
        tar -xOf ${commonmark} package/dist/commonmark.js > commonmark.cjs
        tar -xOf ${commonmark} package/LICENSE > license
      ''}
      ${lib.optionalString (name == "plotly") ''
        tar -xOf ${plotly} package/plotly.min.js > plotly.cjs
        tar -xOf ${plotly} package/LICENSE > license
      ''}
      esbuild ${name}.js --bundle --format=iife --platform=browser --target=es2022 \
        --minify --legal-comments=inline --outfile=view.js
      cat > "$out" <<'HTML'
      <!doctype html>
      <html lang="en">
      <meta charset="utf-8">
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <meta http-equiv="Content-Security-Policy" content="default-src 'none'; connect-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src blob: data:; font-src data:">
      <title>${name}</title>
      <style>
        html { color-scheme: light dark; font: 14px system-ui, sans-serif; }
        body { margin: 0; }
        main { height: 100dvh; overflow: auto; }
        [role="status"] { margin: 0; height: 100%; display: grid; place-items: center; color: GrayText; }
        pre { margin: 1em; overflow-wrap: anywhere; }
      </style>
      <main><p role="status">No data yet</p></main>
      <script>
      HTML
      cat view.js >> "$out"
      printf '</script>\n' >> "$out"
      if [ -f license ]; then
        printf '<!--\n' >> "$out"
        cat license >> "$out"
        printf '\n-->\n' >> "$out"
      fi
      printf '</html>\n' >> "$out"
    '';
  };
  charts = map bundle [
    "line-chart"
    "points"
  ];
  common =
    map bundle [
      "text"
      "markdown"
      "table"
      "image"
    ]
    ++ charts;
  plotlyViews = common ++ [ (bundle "plotly") ];
in
{
  inherit charts common plotlyViews;
}
