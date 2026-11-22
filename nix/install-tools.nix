# Populate the Card package's bin directory with its declared tools.
# Keep the first occurrence of a command, as when searching multiple PATH entries.
{ lib }:
tools:
lib.optionalString (tools != [ ]) ''
  mkdir -p "$out/bin"
  for tool in ${lib.escapeShellArgs (map (tool: "${lib.getBin tool}/bin") tools)}; do
    for executable in "$tool"/*; do
      if [ -f "$executable" ] && [ -x "$executable" ]; then
        target="$out/bin/''${executable##*/}"
        if [ ! -L "$target" ]; then
          ln -s "$executable" "$target"
        fi
      fi
    done
  done
''
