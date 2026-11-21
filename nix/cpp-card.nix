{
  lib,
  stdenv,
  runCommand,
  runCommandCC,
  flatbuffers,
  python3,
  runtime,
}:
{
  # Pass the Card definition directory as src for definition-relative failure locations.
  cppCard =
    {
      src,
      contract,
      buildInputs ? [ ],
      cxxflags ? [ ],
    }:
    let
      generated =
        runCommand "tsugumi-cpp-card-headers"
          {
            nativeBuildInputs = [
              flatbuffers
              python3
            ];
          }
          ''
            contract=${lib.escapeShellArg "${contract}"}
            mkdir -p "$out/include"
            for schema in _inputs _outputs _params; do
              flatc --cpp --gen-object-api --scoped-enums -o "$out/include" "$contract/$schema.fbs"
            done
            python3 ${../sdk/cpp/card_cpp.py} "$contract/_contract.json" "$out"
          '';
    in
    runCommandCC "tsugumi-cpp-card"
      {
        buildInputs = [ flatbuffers ] ++ buildInputs;
      }
      ''
        mkdir -p "$out/lib"
        ${
          if stdenv.hostPlatform.isDarwin then
            ''
              link=(-Wl,-exported_symbol,_tsugumi_main -Wl,-undefined,error)
            ''
          else
            ''
              printf '{ global: tsugumi_main; local: *; };\n' > "$TMPDIR/exports"
              link=(-Wl,--version-script,"$TMPDIR/exports" -Wl,-z,defs)
            ''
        }
        cd ${lib.escapeShellArg "${src}"}
        find . -type f -name '*.cc' ! -name '._*' -print0 | LC_ALL=C sort -z > "$TMPDIR/tsugumi-sources"
        mapfile -d "" -t sources < "$TMPDIR/tsugumi-sources"
        if (( ''${#sources[@]} == 0 )); then
          echo "cppCard: no .cc sources in src" >&2
          exit 1
        fi
        # Pack's assertions are disabled so the adapter can verify malformed outputs
        # and report them as Card failures.
        $CXX -std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
          -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough \
          -shared "''${link[@]}" -I"${generated}/include" -I"${runtime}/include" \
          -I"${../sdk/cpp/src}" -ffile-prefix-map="$PWD"=. \
          "''${sources[@]}" "${generated}/src/card_adapter.cc" \
          "${runtime}/lib/libtsugumi_abi9.a" ${lib.escapeShellArgs cxxflags} -pthread -o "$out/lib/main.so"
      '';
}
