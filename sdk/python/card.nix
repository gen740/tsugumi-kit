{
  lib,
  stdenv,
  runCommand,
  runCommandCC,
  flatbuffers,
  python3,
  ipaexGothic,
  portCodegen,
  cardInterface,
  publishApi,
  runtime,
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
    runCommand "tsugumi-python-card-bindings"
      {
        nativeBuildInputs = [
          flatbuffers
          (python3.withPackages (_: [ portCodegen ]))
        ];
      }
      ''
        abi=${../../abi}
        PYTHONPATH="${../cpp}''${PYTHONPATH:+:$PYTHONPATH}" \
          python3 ${./generate.py} ${lib.escapeShellArg "${interface}"} "$out" \
          ${flatbuffers.src}/reflection/reflection.fbs
        flatc --python --gen-object-api -o "$out/wire" "$abi/card_enums.fbs" "$abi/card.fbs"
      '';
  pythonCard =
    {
      src,
      inputs,
      outputs,
      params ? { },
      python ? python3,
      packages ? (_: [ ]),
      tools ? [ ],
    }:
    let
      inherit (stdenv.hostPlatform) isDarwin;
      pythonEnv = python.withPackages (ps: [ ps.flatbuffers ] ++ packages ps);
      libpython = "${python}/lib/libpython${python.pythonVersion}${stdenv.hostPlatform.extensions.sharedLibrary}";
      bindings = generate { inherit inputs outputs params; };
    in
    runCommandCC "tsugumi-python-card"
      {
        buildInputs = [
          flatbuffers
          python
        ];
      }
      ''
        src=${lib.escapeShellArg "${src}"}
        test -f "$src/main.py" || { echo "pythonCard: src must contain main.py" >&2; exit 1; }
        mkdir -p "$out/lib/python/tsugumi" "$out/lib/python-wire"
        cp -R --no-preserve=mode "${bindings}/python/." "$out/lib/python/"
        cp -R "${./tsugumi}/." "$out/lib/python/tsugumi/"
        if [ -d "${pythonEnv}/${python.sitePackages}/matplotlib" ]; then
          ln -s ${ipaexGothic}/share/fonts/truetype/ipaexg.ttf "$out/lib/python/tsugumi/"
        fi
        # Tsugumi and tsugumi need distinct roots on case-insensitive filesystems.
        cp -R "${bindings}/wire/." "$out/lib/python-wire/"
        ${
          if isDarwin then
            ''
              link=(-Wl,-exported_symbol,_tsugumi_main -Wl,-undefined,error)
            ''
          else
            ''
              printf '{ global: tsugumi_main; local: *; };\n' > "$TMPDIR/exports"
              link=(-Wl,--version-script,"$TMPDIR/exports" -Wl,-z,defs)
            ''
        }
        ${import ../../nix/prepare-tools.nix { inherit lib; } tools}
        $CXX -std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
          -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough \
          -shared "''${toolPathObject[@]}" "''${link[@]}" -I"${bindings}/include" -I"${runtime}/include" \
          -I"${./.}" -I"${../cpp/src}" -I"${python}/include/python${python.pythonVersion}" \
          "-DTSUGUMI_PY_HOME=\"${python}\"" "-DTSUGUMI_LIBPYTHON=\"${libpython}\"" \
          "-DTSUGUMI_PY_SDK=\"$out/lib/python\"" "-DTSUGUMI_PY_SOURCE=\"$src\"" \
          "-DTSUGUMI_PY_WIRE=\"$out/lib/python-wire\"" \
          "-DTSUGUMI_PY_PACKAGES=\"${pythonEnv}/${python.sitePackages}\"" \
          "${./.}/python_card.cc" "${bindings}/card_adapter.cc" \
          "${runtime}/lib/libtsugumi_abi.a" "${libpython}" \
          -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o "$out/lib/main.so"
      '';
  pythonApi =
    args:
    let
      bindings = generate args;
    in
    publishApi "python" ''
      cp "${bindings}/README.md" "$api/"
      cd "${bindings}/python"
      find . -name '*.pyi' -print0 | while IFS= read -r -d "" stub; do
        install -Dm644 "$stub" "$api/$stub"
      done
    '';
in
{
  inherit pythonCard pythonApi;
}
