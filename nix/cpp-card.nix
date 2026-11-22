{
  lib,
  stdenv,
  runCommand,
  runCommandCC,
  flatbuffers,
  python3,
  runtime,
}:
let
  buildCard =
    {
      src,
      contract,
      definitionDir,
      buildInputs,
      cxxflags,
      tools,
    }:
    let
      definition = toString definitionDir;
      source = toString src;
      sourceFlags = lib.optionals (source == definition || lib.hasPrefix "${definition}/" source) [
        "-ffile-prefix-map=${src}=${
          if source == definition then "." else lib.removePrefix "${definition}/" source
        }"
      ];
      generated =
        runCommand "tsugumi-cpp-card-headers"
          {
            nativeBuildInputs = [
              flatbuffers
              python3
            ];
          }
          ''
            python3 ${../sdk/cpp/card_cpp.py} ${lib.escapeShellArg "${contract}/_contract.json"} "$out" \
              ${flatbuffers.src}/reflection/reflection.fbs
          '';
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
          "${runtime}/lib/libtsugumi_abi9.a" ${lib.escapeShellArgs cxxflags} -pthread -o "$out/lib/main.so"
      '';
in
{
  # Omit src to bind the core's contract and definition directory before
  # injecting this builder into the Card's default.nix.
  cppCard =
    {
      contract,
      src ? null,
      definitionDir ? src,
      buildInputs ? [ ],
      cxxflags ? [ ],
      tools ? [ ],
    }:
    let
      withContract =
        {
          src,
          buildInputs ? [ ],
          cxxflags ? [ ],
          tools ? [ ],
        }:
        buildCard {
          inherit
            src
            contract
            definitionDir
            buildInputs
            cxxflags
            tools
            ;
        };
    in
    if src != null then
      withContract {
        inherit
          src
          buildInputs
          cxxflags
          tools
          ;
      }
    else
      assert lib.assertMsg (
        definitionDir != null && buildInputs == [ ] && cxxflags == [ ] && tools == [ ]
      ) "cppCard: bind contract and definitionDir first, then pass src, buildInputs, cxxflags and tools";
      withContract;
}
