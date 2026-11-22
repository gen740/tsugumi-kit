# The worker uses the Card package's bin directory as its runtime PATH.
# Keep the first occurrence of a command, as when searching multiple PATH entries.
{ lib }:
tools:
lib.optionalString (tools != [ ]) ''
  for tool in ${lib.escapeShellArgs (map (tool: "${lib.getBin tool}/bin") tools)}; do
    for executable in "$tool"/*; do
      if [ -f "$executable" ] && [ -x "$executable" ]; then
        mkdir -p "$out/bin"
        target="$out/bin/''${executable##*/}"
        if [ ! -L "$target" ]; then
          ln -s "$executable" "$target"
        fi
      fi
    done
  done
''
