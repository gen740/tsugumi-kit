# Rewrite links to libraries staged by the Python runtime package.
relink_python() {
  local root="$1" binary links rpath replacement source target index
  local sources=("$originalLibrary") targets=("$runtimeLibrary")
  if [ "$pythonDarwin" = 1 ] && [ -f "$originalFfi/libffi-trampolines.dylib" ]; then
    source="$(realpath "$originalFfi/libffi.dylib")"
    sources+=("$source")
    targets+=("$(dirname "$runtimeLibrary")/$(basename "$source")")
  fi
  while IFS= read -r -d "" binary; do
    if [ "$pythonDarwin" = 1 ]; then
      links="$(otool -l "$binary")"
      local changes=()
      for index in "${!sources[@]}"; do
        source="${sources[$index]}"
        target="${targets[$index]}"
        if [[ "$links" == *"name $source (offset "* ]]; then
          replacement="$(realpath --relative-to="$(dirname "$binary")" "$target")"
          changes+=(-change "$source" "@loader_path/$replacement")
        fi
      done
      if [[ "$links" == *"path $originalPython/lib (offset "* ]]; then
        replacement="$(realpath --relative-to="$(dirname "$binary")" "$(dirname "$runtimeLibrary")")"
        changes+=(-rpath "$originalPython/lib" "@loader_path/$replacement")
      fi
      if [ "${#changes[@]}" -gt 0 ]; then
        install_name_tool "${changes[@]}" "$binary"
        codesign -f -s - "$binary"
      fi
    else
      rpath="$(patchelf --print-rpath "$binary")"
      replacement="${rpath//"$originalPython/lib"/"$(dirname "$runtimeLibrary")"}"
      if [ "$replacement" != "$rpath" ]; then
        patchelf --set-rpath "$replacement" "$binary"
      fi
    fi
  done < <(find "$root" -type f \( -name '*.so' -o -name '*.so.*' -o -name '*.dylib' \) -print0)
}
