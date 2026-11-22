{
  lib,
  stdenv,
  runCommand,
  runCommandCC,
  flatbuffers,
  python3,
  typescript,
  nodejs_24,
  libuv,
  importNpmLock,
  libnode,
  runtime,
}:
let
  # Use the JS implementation from the same FlatBuffers source as flatc.
  flatbuffersJs =
    runCommand "tsugumi-flatbuffers-js-${flatbuffers.version}"
      {
        nativeBuildInputs = [ typescript ];
      }
      ''
        cp -r ${flatbuffers.src}/ts ts
        mkdir "$out"
        cp ${flatbuffers.src}/LICENSE "$out/"
        tsc --target es2020 --module es2020 --declaration --strict \
          --rootDir ts --outDir "$out" ts/flatbuffers.ts
        cat > "$out/package.json" <<'JSON'
        {"name":"flatbuffers","type":"module","main":"./flatbuffers.js","types":"./flatbuffers.d.ts"}
        JSON
      '';
in
{
  src,
  contract,
  tools ? [ ],
  buildInputs ? [ ],
  nativeBuildInputs ? [ ],
}:
let
  inherit (stdenv.hostPlatform) isDarwin;
  abi = ../abi/9;
  sdk = lib.fileset.toSource {
    root = ../sdk/js;
    fileset = lib.fileset.difference ../sdk/js ../sdk/js/tests;
  };
  npmModules =
    if builtins.pathExists (src + "/package.json") then
      importNpmLock.buildNodeModules {
        npmRoot = src;
        nodejs = nodejs_24;
        derivationArgs = {
          inherit buildInputs nativeBuildInputs;
          npmInstallFlags = [ "--omit=dev" ];
          npmRebuildFlags = [ "--omit=dev" ];
        };
      }
    else
      null;
  bindings =
    runCommand "tsugumi-js-card-bindings"
      {
        nativeBuildInputs = [
          flatbuffers
          python3
          typescript
        ];
      }
      ''
        contract=${lib.escapeShellArg "${contract}"}
        mkdir -p generated node_modules "$out/js/node_modules"
        ln -s ${flatbuffersJs} node_modules/flatbuffers
        ln -s ${flatbuffersJs} "$out/js/node_modules/flatbuffers"
        cp ${sdk}/package.json .
        flatc --ts --gen-object-api --gen-all -I "$contract" -o generated \
          "$contract/_inputs.fbs" "$contract/_outputs.fbs" "$contract/_params.fbs" "${abi}/card.fbs"
        python3 "${sdk}/generate.py" "$contract" "${../sdk/cpp}" \
          "${flatbuffers.src}/reflection/reflection.fbs" "$out/native" generated
        tsc --target es2022 --module nodenext --declaration --noCheck \
          --rootDir generated --outDir "$out/js" generated/bindings.ts generated/tsugumi/card.ts
        cp ${sdk}/{package.json,bootstrap.cjs,index.js,index.d.ts,effects.js,display.js,failure.js,runtime.js} "$out/js/"
      '';
in
runCommandCC "tsugumi-js-card"
  {
    passthru = { inherit bindings; };
    buildInputs = [
      flatbuffers
      libuv
    ];
  }
  ''
    src=${lib.escapeShellArg "${src}"}
    test -f "$src/main.mjs" || { echo "jsCard: src must contain main.mjs" >&2; exit 1; }
    app="$out/lib/card"
    mkdir -p "$app/node_modules/@tsugumi"
    # The installed node_modules comes exclusively from the lockfile.
    tar --exclude=./node_modules -C "$src" -cf - . | tar -x -C "$app"
    chmod u+w "$app"
    ${lib.optionalString (npmModules != null) ''
      if [ -d "${npmModules}/node_modules" ]; then
        cp -r --no-preserve=mode "${npmModules}/node_modules/." "$app/node_modules/"
      fi
    ''}
    ln -s "${bindings}/js" "$app/node_modules/@tsugumi/card"
    ${
      if isDarwin then
        ''
          link=(-Wl,-exported_symbol,_tsugumi_main -Wl,-undefined,error)
          libraries=(${libnode}/lib/libnode.*.dylib)
        ''
      else
        ''
          printf '{ global: tsugumi_main; local: *; };\n' > "$TMPDIR/exports"
          link=(-Wl,--version-script,"$TMPDIR/exports" -Wl,-z,defs)
          libraries=(${libnode}/lib/libnode.so.*)
        ''
    }
    test "''${#libraries[@]}" = 1 && test -f "''${libraries[0]}"
    ${import ./prepare-tools.nix { inherit lib; } tools}
    $CXX -std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
      -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough \
      -shared "''${toolPathObject[@]}" "''${link[@]}" -I"${bindings}/native/include" -I"${runtime}/include" \
      -I"${sdk}" -I"${../sdk/cpp/src}" -isystem "${libnode}/include/node" \
      "-DTSUGUMI_NODE=\"${libnode}/bin/node\"" "-DTSUGUMI_LIBNODE=\"''${libraries[0]}\"" \
      "-DTSUGUMI_JS_BOOTSTRAP=\"${bindings}/js/bootstrap.cjs\"" "-DTSUGUMI_JS_ENTRY=\"$app/main.mjs\"" \
      "${sdk}/node_card.cc" "${bindings}/native/card_adapter.cc" \
      "${runtime}/lib/libtsugumi_abi9.a" "''${libraries[0]}" -Wl,-rpath,"${libnode}/lib" -luv \
      -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o "$out/lib/main.so"
  ''
