# Supply a PATH initializer for main.so only when the Card declares tools.
{ lib }:
tools:
''
  toolPathObject=()
''
+ lib.optionalString (tools != [ ]) ''
  ${import ./install-tools.nix { inherit lib; } tools}
  $CC -std=c11 -Wall -Wextra -Werror -fPIC \
    "-DTSUGUMI_TOOL_PATH=\"$out/bin\"" -c ${./tool-path.c} -o "$TMPDIR/tsugumi-tool-path.o"
  toolPathObject=("$TMPDIR/tsugumi-tool-path.o")
''
