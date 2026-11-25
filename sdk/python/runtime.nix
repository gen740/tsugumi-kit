{
  darwin,
  libffi,
  stdenv,
  runCommandCC,
  removeReferencesTo,
  patchelf,
  python,
  tzdata,
  mailcap,
  cacert,
}:
let
  inherit (stdenv.hostPlatform) isDarwin;
  inherit (stdenv.hostPlatform.extensions) sharedLibrary;
  library = "libpython${python.pythonVersion}${sharedLibrary}";
in
runCommandCC "tsugumi-python-runtime-${python.version}"
  {
    nativeBuildInputs = [
      removeReferencesTo
    ]
    ++ (if isDarwin then [ darwin.sigtool ] else [ patchelf ]);
    disallowedRequisites = [ python ];
  }
  ''
    originalPython=${python}
    originalLibrary="$originalPython/lib/${library}"
    runtimeLibrary="$out/lib/${library}"
    pythonDarwin=${if isDarwin then "1" else "0"}
    stdlib="$out/lib/python${python.pythonVersion}"
    mkdir -p "$stdlib" "$out/bin" "$out/share"
    cp -LR --no-preserve=mode "$originalPython/lib/python${python.pythonVersion}/." "$stdlib/"
    cp --no-preserve=mode "$originalLibrary" "$runtimeLibrary"
    cp --no-preserve=mode ${./bootstrap.py} "$out/lib/tsugumi-bootstrap.py"
    cp --no-preserve=mode ${mailcap}/etc/mime.types "$out/share/mime.types"
    cp --no-preserve=mode ${cacert}/etc/ssl/certs/ca-bundle.crt "$out/share/"
    cp -R --no-preserve=mode ${tzdata}/share/zoneinfo "$out/share/"
    if [ -d "$originalPython/share/licenses" ]; then
      cp -LR --no-preserve=mode "$originalPython/share/licenses" "$out/share/"
    fi

    rm -rf "$stdlib"/config-* "$stdlib"/site-packages \
      "$stdlib"/_sysconfig_vars_*.json "$stdlib"/build-details.json "$stdlib"/ensurepip \
      "$stdlib"/ctypes/macholib/fetch_macholib
    find "$stdlib" -type d -name __pycache__ -prune -exec rm -rf {} +
    "${python}/bin/${python.executable}" ${./runtime-files.py} stdlib "$stdlib" "$originalPython"

    # CPython's fallback compiled-in prefix is superseded by PyConfig.home
    # in the embedded interpreter and by landmark discovery in the CLI.
    remove-references-to -t "$originalPython" "$runtimeLibrary"
    ${
      if isDarwin then
        ''
          install_name_tool -id "@rpath/${library}" "$runtimeLibrary"
          codesign -f -s - "$runtimeLibrary"
          originalFfi=${libffi}/lib
          if [ -f "$originalFfi/libffi-trampolines.dylib" ]; then
            ffi="$(basename "$(realpath "$originalFfi/libffi.dylib")")"
            for dylib in "$ffi" libffi-trampolines.dylib; do
              cp --no-preserve=mode "$originalFfi/$dylib" "$out/lib/"
              install_name_tool -id "@rpath/$dylib" "$out/lib/$dylib"
            done
            "${python}/bin/${python.executable}" ${./runtime-files.py} ffi "$out/lib/$ffi" "$originalFfi"
            codesign -f -s - "$out/lib/$ffi" "$out/lib/libffi-trampolines.dylib"
          fi
        ''
      else
        ''
          soname="$(patchelf --print-soname "$runtimeLibrary")"
          if [ -n "$soname" ] && [ "$soname" != "${library}" ]; then
            ln -s "${library}" "$out/lib/$soname"
          fi
        ''
    }
    source ${./relocate-libpython.sh}
    relink_python "$stdlib"

    cp ${./interpreter.cc} interpreter.cc
    cp ${./image_path.hh} image_path.hh
    ${
      if isDarwin then
        ''
          interpreterLink=(-Wl,-install_name,"$out/lib/interpreter${sharedLibrary}" -Wl,-headerpad_max_install_names -Wl,-rpath,@loader_path)
          cliLink=(-Wl,-headerpad_max_install_names -Wl,-rpath,@loader_path/../lib)
        ''
      else
        ''
          interpreterLink=(-ldl '-Wl,-rpath,$ORIGIN')
          cliLink=('-Wl,-rpath,$ORIGIN/../lib')
        ''
    }
    $CXX -std=c++20 -O2 -fPIC -fvisibility=hidden -shared \
      -I"${python}/include/python${python.pythonVersion}" \
      "-DTSUGUMI_LIBPYTHON_NAME=\"${library}\"" "-DTSUGUMI_PYTHON_NAME=\"${python.executable}\"" \
      interpreter.cc "$runtimeLibrary" "''${interpreterLink[@]}" \
      -o "$out/lib/interpreter${sharedLibrary}"

    # The relocatable CLI also gives embedded code a usable sys.executable.
    cat > python.c <<'EOF'
    #include <Python.h>
    int main(int argc, char** argv) { return Py_BytesMain(argc, argv); }
    EOF
    $CC -O2 -I"${python}/include/python${python.pythonVersion}" python.c \
      "$runtimeLibrary" "''${cliLink[@]}" \
      -o "$out/bin/${python.executable}"
  ''
