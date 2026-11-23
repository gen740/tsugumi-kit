{
  lib,
  stdenv,
  runCommand,
  runCommandCC,
  flatbuffers,
  python3,
  runtime,
  portCodegen,
  cardInterface,
  publishApi,
}:
let
  generate =
    {
      inputs,
      outputs,
      params ? { },
    }:
    let
      interface = cardInterface { inherit inputs outputs params; };
    in
    runCommand "tsugumi-cpp-card-headers"
      {
        nativeBuildInputs = [
          flatbuffers
          (python3.withPackages (_: [ portCodegen ]))
        ];
      }
      ''
        python3 ${../sdk/cpp/card_cpp.py} ${lib.escapeShellArg "${interface}"} "$out" \
          ${flatbuffers.src}/reflection/reflection.fbs
      '';

in
{
  cppCard =
    {
      src,
      inputs,
      outputs,
      params ? { },
      buildInputs ? [ ],
      cxxflags ? [ ],
      tools ? [ ],
    }:
    let
      sourceFlags = [ "-ffile-prefix-map=${src}=${baseNameOf (toString src)}" ];
      generated = generate { inherit inputs outputs params; };
    in
    runCommandCC "tsugumi-cpp-card"
      {
        passthru = { inherit generated; };
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
        find "$PWD" -type f -name '*.cc' ! -name '._*' -print0 | LC_ALL=C sort -z > "$TMPDIR/tsugumi-sources"
        mapfile -d "" -t sources < "$TMPDIR/tsugumi-sources"
        if (( ''${#sources[@]} == 0 )); then
          echo "cppCard: no .cc sources in src" >&2
          exit 1
        fi
        # Keep the post-Pack verifier active even for failures detected only
        # in the serialized representation.
        ${import ./prepare-tools.nix { inherit lib; } tools}
        $CXX -std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
          -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough \
          -shared "''${toolPathObject[@]}" "''${link[@]}" -I"${generated}/include" -I"${runtime}/include" \
          -I"${../sdk/cpp/src}" ${lib.escapeShellArgs sourceFlags} \
          "''${sources[@]}" "${generated}/src/card_adapter.cc" \
          "${runtime}/lib/libtsugumi_abi.a" ${lib.escapeShellArgs cxxflags} -pthread -o "$out/lib/main.so"
      '';
  cppApi =
    args:
    let
      generated = generate args;
    in
    publishApi "cpp" ''
      cp "${generated}/README.md" "${generated}/include/"*.h "${generated}/include/card.hh" "$api/"
    '';
}
