{
  pkgs,
  abi,
  portCodegen,
  paramValues,
  cardInterface,
  publishApi,
}:
let
  generate =
    {
      inputs ? null,
      outputs,
      params ? { },
      embed ? false,
    }:
    let
      interface = cardInterface { inherit inputs outputs params; };
    in
    pkgs.runCommand "tsugumi-rust-sdk"
      {
        nativeBuildInputs = [
          pkgs.flatbuffers
          (pkgs.python3.withPackages (_: [ portCodegen ]))
        ];
      }
      ''
        mkdir -p "$out/src"
        cp ${./Cargo.toml} "$out/Cargo.toml"
        cp ${./src}/*.rs "$out/src/"
        ln -s ${pkgs.flatbuffers.src}/rust/flatbuffers "$out/flatbuffers"
        for schema in "${interface}"/tsugumi/card.fbs "${interface}"/_{inputs,outputs}.fbs; do
          flatc --rust --gen-all --gen-object-api -I "${interface}" -o "$out/src" "$schema"
        done
        python3 ${./card_rust.py} "${interface}" "$out" \
          ${pkgs.flatbuffers.src}/reflection/reflection.fbs ${pkgs.lib.optionalString embed "${paramValues params}"}
      '';
  rustCard =
    {
      src,
      inputs ? null,
      outputs,
      params ? { },
      name ? "tsugumi-rust-card",
      lock ? null,
      sourcePrefix ? "",
      buildInputs ? [ ],
      nativeBuildInputs ? [ ],
      tools ? [ ],
    }:
    let
      inherit (pkgs) lib;
      manifest = builtins.fromTOML (builtins.readFile (lib.path.append src "Cargo.toml"));
      library = manifest.lib.name or (builtins.replaceStrings [ "-" ] [ "_" ] manifest.package.name);
      target = pkgs.stdenv.hostPlatform.rust.rustcTarget;
      isDarwin = pkgs.stdenv.hostPlatform.isDarwin;
      cargoLock = if lock == null then ./Cargo.lock else lock;
      vendor = pkgs.rustPlatform.importCargoLock { lockFile = cargoLock; };
      sdk = generate {
        inherit inputs outputs params;
        embed = true;
      };
    in
    assert lib.assertMsg (lib.elem "staticlib" (
      manifest.lib.crate-type or [ ]
    )) "rustCard: add crate-type = [\"staticlib\"] to [lib] in Cargo.toml";
    pkgs.runCommandCC name
      {
        inherit buildInputs;
        nativeBuildInputs = [
          pkgs.cargo
          pkgs.python3
          pkgs.rustc
        ]
        ++ nativeBuildInputs;
        # Unwind Card panics to the adapter for a structured failure.
        CARGO_PROFILE_RELEASE_PANIC = "unwind";
        CARGO_PROFILE_RELEASE_STRIP = "debuginfo";
        TSUGUMI_RUST_SOURCE_PREFIX = sourcePrefix;
        passthru = { inherit sdk; };
      }
      ''
        mkdir -p card "$out/lib"
        cp -r --no-preserve=mode ${src}/. card/
        cd card
        if [ -e tsugumi ] || [ -L tsugumi ]; then
          echo "rustCard: tsugumi is reserved for the generated SDK" >&2
          exit 1
        fi
        ln -s ${sdk} tsugumi
        install -m 0644 ${cargoLock} Cargo.lock

        export CARGO_HOME="$TMPDIR/cargo-home"
        mkdir -p "$CARGO_HOME"
        cat > "$CARGO_HOME/config.toml" <<'TOML'
        [source.crates-io]
        replace-with = "vendored-sources"
        [source.vendored-sources]
        directory = "${vendor}"
        TOML
        set -o pipefail
        target_dir="$TMPDIR/target"
        cargo rustc --lib --release --offline --target-dir "$target_dir" --target ${target} ${
          lib.optionalString (lock != null) "--locked"
        } --message-format=json -- --print=native-static-libs \
          | python3 ${./native_link.py} "$target_dir/${target}" > "$TMPDIR/native-link-args"
        mapfile -t native_link < "$TMPDIR/native-link-args"
        $CC -std=c11 -fPIC -fvisibility=hidden -Wall -Wextra -Werror -I${abi} \
          -c ${./rust_host.c} -o rust_host.o
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
        $CC -shared "''${toolPathObject[@]}" rust_host.o "$target_dir/${target}/release/lib${library}.a" \
          "''${link[@]}" "''${native_link[@]}" \
          -o "$out/lib/main.so"
      '';
  rustApi =
    args:
    let
      sdk = generate args;
    in
    publishApi "rust" ''
      cp ${sdk}/README.md ${sdk}/src/{lib,_inputs_generated,_outputs_generated,card_generated,params}.rs "$api/"
    '';
in
{
  inherit rustCard rustApi;
}
