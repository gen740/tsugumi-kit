{
  lib,
  runCommand,
  fetchurl,
  esbuild,
  view,
}:
let
  commonmark = fetchurl {
    url = "https://registry.npmjs.org/commonmark/-/commonmark-0.31.2.tgz";
    hash = "sha256-2jtA61oGgktGGO1aKWS+w/TmqV/T3ZBHFfkpPHd4KPE=";
  };

  bundle =
    name:
    view {
      name = "tsugumi-cpp-view-${name}";
      kind = "js";
      entry = "index.js";
      src = runCommand "tsugumi-cpp-view-${name}-bundle" { nativeBuildInputs = [ esbuild ]; } ''
        mkdir -p "$out"
        cp ${./.}/{${name},data}.js .
        ${lib.optionalString (name == "markdown") ''
          tar -xOf ${commonmark} package/dist/commonmark.js > commonmark.cjs
          tar -xOf ${commonmark} package/LICENSE > "$out/LICENSE"
        ''}
        esbuild ${name}.js --bundle --format=esm --platform=browser --target=es2022 \
          --minify --outfile="$out/index.js"
      '';
    };
in
lib.genAttrs [
  "text"
  "markdown"
  "table"
  "image"
] bundle
