{
  lib,
  esbuild,
  fetchurl,
  stdenv,
  runCommand,
  runCommandCC,
  flatbuffers,
  python3,
  runtime,
}:
{
  src,
  contract,
  python ? python3,
  packages ? (_: [ ]),
  views ? { },
}:
let
  viewLib = import ../../nix/view.nix { inherit lib runCommand; };
  inherit (stdenv.hostPlatform) isDarwin;
  pythonEnv = python.withPackages (ps: [ ps.flatbuffers ] ++ packages ps);
  libpython = "${python}/lib/libpython${python.pythonVersion}${stdenv.hostPlatform.extensions.sharedLibrary}";
  bindings =
    runCommand "tsugumi-python-card-bindings"
      {
        nativeBuildInputs = [
          flatbuffers
          python3
        ];
      }
      ''
        contract=${lib.escapeShellArg "${contract}"}
        abi=${../../abi/9}
        mkdir -p "$out/include" "$out/python" "$out/wire"
        python3 ${./generate.py} prepare "${../cpp}" "$contract"
        for schema in _inputs _outputs _params; do
          flatc --cpp --scoped-enums -I "$contract" -o "$out/include" "$schema.fbs"
          flatc --python --gen-object-api -I "$contract" -o "$out/python" "$schema.fbs"
        done
        flatc --python --gen-object-api -o "$out/wire" "$abi/tsugumi_enums.fbs" "$abi/card.fbs"
        # Use schema metadata only during generation of the Python union checks.
        flatc --binary --schema -I "$contract" _outputs.fbs
        flatc --json --strict-json --defaults-json --raw-binary \
          ${flatbuffers.src}/reflection/reflection.fbs -- _outputs.bfbs
        python3 ${./generate.py} generate "$out/python/card.py" "$out/card_adapter.cc"
      '';
in
runCommandCC "tsugumi-python-card"
  {
    passthru.views = viewLib.checkViews "pythonCard" (
      import ./views {
        inherit
          lib
          esbuild
          fetchurl
          runCommand
          ;
        inherit (viewLib) view;
      }
      // views
    );
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
    $CXX -std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
      -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough \
      -shared "''${link[@]}" -I"${bindings}/include" -I"${runtime}/include" \
      -I"${./.}" -I"${../cpp/src}" -I"${python}/include/python${python.pythonVersion}" \
      "-DTSUGUMI_PY_HOME=\"${python}\"" "-DTSUGUMI_LIBPYTHON=\"${libpython}\"" \
      "-DTSUGUMI_PY_SDK=\"$out/lib/python\"" "-DTSUGUMI_PY_SOURCE=\"$src\"" \
      "-DTSUGUMI_PY_WIRE=\"$out/lib/python-wire\"" \
      "-DTSUGUMI_PY_PACKAGES=\"${pythonEnv}/${python.sitePackages}\"" \
      "${./.}/python_card.cc" "${bindings}/card_adapter.cc" \
      "${runtime}/lib/libtsugumi_abi9.a" "${libpython}" \
      -pthread ${lib.optionalString (!isDarwin) "-ldl"} -o "$out/lib/main.so"
  ''
