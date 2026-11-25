{
  lib,
  darwin,
  stdenv,
  runCommand,
  runCommandCC,
  flatbuffers,
  libffi,
  patchelf,
  python3,
  pythonRuntime,
  ipaexGothic,
  portCodegen,
  cardInterface,
  publishApi,
  paramValues,
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
        PYTHONPATH="${../cpp}''${PYTHONPATH:+:$PYTHONPATH}" \
          python3 ${./generate.py} ${lib.escapeShellArg "${interface}"} "$out" \
          ${flatbuffers.src}/reflection/reflection.fbs

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
      inherit (stdenv.hostPlatform.extensions) sharedLibrary;
      interpreter = pythonRuntime python;
      library = "libpython${python.pythonVersion}${sharedLibrary}";
      bindings = generate { inherit inputs outputs params; };
    in
    runCommandCC "tsugumi-python-card"
      {
        nativeBuildInputs = [
          (python3.withPackages (_: [ portCodegen ]))
        ]
        ++ (if isDarwin then [ darwin.sigtool ] else [ patchelf ]);
        disallowedReferences = [
          python
          pythonEnv
        ];
        disallowedRequisites = lib.optionals (tools == [ ]) [
          python
          pythonEnv
        ];
        buildInputs = [
          flatbuffers
          python
        ];
      }
      ''
        src=${lib.escapeShellArg "${src}"}
        test -f "$src/main.py" || { echo "pythonCard: src must contain main.py" >&2; exit 1; }
        mkdir -p "$out/lib/python/tsugumi" "$out/lib/python-wire" \
          "$out/lib/python-source" "$out/lib/python-packages" "$out/share/tsugumi"
        cp -R --no-preserve=mode "$src/." "$out/lib/python-source/"
        cp -R --no-preserve=mode "${bindings}/python/." "$out/lib/python/"
        cp -R "${./tsugumi}/." "$out/lib/python/tsugumi/"
        cp -LR --no-preserve=mode "${pythonEnv}/${python.sitePackages}/." "$out/lib/python-packages/"
        rm -f "$out/lib/python-packages"/_sysconfig{data_,_vars_}* \
          "$out/lib/python-packages/sitecustomize.py"
        find "$out/lib/python-packages" -type d -name __pycache__ -prune -exec rm -rf {} +
        originalPython=${python}
        originalLibrary="$originalPython/lib/${library}"
        runtimeLibrary="${interpreter}/lib/${library}"
        pythonDarwin=${if isDarwin then "1" else "0"}
        ${lib.optionalString isDarwin "originalFfi=${libffi}/lib"}
        python3 ${./runtime-files.py} site-packages "$out/lib/python-packages" "$originalPython"
        source ${./relocate-libpython.sh}
        relink_python "$out/lib/python-packages"

        cat > "$out/share/tsugumi/runtime.json" <<'EOF'
        ${builtins.toJSON {
          version = 1;
          runtimes = [
            {
              name = "python";
              roots = [ "${interpreter}" ];
            }
          ];
        }}
        EOF
        PYTHONPATH="${../cpp}''${PYTHONPATH:+:$PYTHONPATH}" \
          python3 ${./generate.py} --params "${cardInterface { inherit inputs outputs params; }}" \
            "${paramValues params}" "$out/lib/python/card_params.py"
        if [ -d "${pythonEnv}/${python.sitePackages}/matplotlib" ]; then
          cp ${ipaexGothic}/share/fonts/truetype/ipaexg.ttf "$out/lib/python/tsugumi/"
        fi
        # Tsugumi and tsugumi need distinct roots on case-insensitive filesystems.
        cp -R "${bindings}/wire/." "$out/lib/python-wire/"
        ${
          if isDarwin then
            ''
              link=(-Wl,-exported_symbol,_tsugumi_main -Wl,-undefined,error -Wl,-headerpad_max_install_names)
            ''
          else
            ''
              printf '{ global: tsugumi_main; local: *; };\n' > "$TMPDIR/exports"
              link=(-Wl,--version-script,"$TMPDIR/exports" -Wl,-z,defs)
            ''
        }
        ${import ../../nix/install-tools.nix { inherit lib; } tools}
        $CXX -std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
          -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough \
          -shared "''${link[@]}" -I"${bindings}/include" -I"${runtime}/include" \
          -I"${./.}" -I"${../cpp/src}" -I"${python}/include/python${python.pythonVersion}" \
          ${lib.optionalString (tools != [ ]) "-DTSUGUMI_PY_HAS_TOOLS"} \
          "${./.}/python_card.cc" "${bindings}/card_adapter.cc" \
          "${runtime}/lib/libtsugumi_abi.a" "$runtimeLibrary" \
          -Wl,-rpath,"${interpreter}/lib" \
          "${interpreter}/lib/interpreter${sharedLibrary}" \
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
