{ runCommand }:
language: install:
runCommand "tsugumi-${language}-api" { } ''
  api="$out/share/tsugumi/api"
  mkdir -p "$api"
  ${install}
''
