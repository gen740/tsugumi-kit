{ pkgs, runtime }:
let
  inherit (pkgs) lib;
  sdk = ../sdk/go;
  nativeFlags = ''
    cxx_flags=(-std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden
      -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough)
  '';
  mkGoCardSdk =
    {
      contract,
      goMod,
      sourcePrefix ? "src",
    }:
    pkgs.runCommandCC "tsugumi-go-sdk"
      {
        nativeBuildInputs = [
          pkgs.go
          pkgs.flatbuffers
          pkgs.python3
        ];
        buildInputs = [ pkgs.flatbuffers ];
        env = {
          GOTOOLCHAIN = "local";
          GOWORK = "off";
          GOPROXY = "off";
        };
      }
      ''
        mkdir -p "$out/flatbuffers" "$out/lib" include module
        cp ${sdk}/{go.mod,runtime.go,display.go,native.go,host.h} "$out/"
        for file in ${pkgs.flatbuffers.src}/go/*.go; do
          case "$file" in *_test.go|*/doc.go) continue;; esac
          cp "$file" "$out/flatbuffers/"
        done

        contract=${lib.escapeShellArg "${contract}"}
        abi=${../abi/9}
        flatc --go --gen-object-api --go-import tsugumi/flatbuffers --go-module-name tsugumi \
          -o "$out" "$abi/tsugumi_enums.fbs" "$abi/card.fbs"
        for schema in _inputs _outputs _params; do
          flatc --go --gen-object-api --go-import tsugumi/flatbuffers --go-module-name tsugumi \
            -o "$out" "$contract/$schema.fbs"
          flatc --cpp --scoped-enums -o include "$contract/$schema.fbs"
        done
        python3 ${sdk}/card_go.py "$contract/_contract.json" "$out"

        cp ${lib.escapeShellArg "${goMod}"} module/go.mod
        export GOCACHE="$TMPDIR/go-cache" GOMODCACHE="$TMPDIR/go-modules"
        module=$(go list -C module -m -f '{{.Path}}')
        python3 - "$module" ${lib.escapeShellArg sourcePrefix} "$out" <<'CODE'
        import json
        import sys
        from pathlib import Path

        module, prefix = map(json.dumps, sys.argv[1:3])
        (Path(sys.argv[3]) / "card_path.go").write_text(
            f"package tsugumi\nconst cardModule = {module}\nconst sourcePrefix = {prefix}\n"
        )
        CODE

        ${nativeFlags}
        mv "$out/card_ports.cc" .
        for source in ${sdk}/{host,entry}.cc card_ports.cc; do
          $CXX "''${cxx_flags[@]}" -Iinclude -I${sdk} -I${runtime}/include \
            -I${../sdk/cpp/src} -c "$source"
        done
        $AR rcs "$out/lib/go-host.a" host.o card_ports.o entry.o
        cp ${runtime}/lib/libtsugumi_abi9.a "$out/lib/"
      '';
  mkGoCard =
    {
      src,
      contract,
      name ? "tsugumi-go-card",
      sourcePrefix ? "src",
      lock ? null,
      hash ? null,
      buildInputs ? [ ],
      nativeBuildInputs ? [ ],
    }:
    assert lib.assertMsg (
      (lock == null) == (hash == null)
    ) "goCard: supply lock (go.sum) and hash together for external Go modules";
    let
      generatedSdk = mkGoCardSdk {
        inherit contract sourcePrefix;
        goMod = "${src}/go.mod";
      };
      inherit (pkgs.stdenv.hostPlatform) isDarwin;
      checkLock = lib.optionalString (lock != null) ''
        cmp -s go.sum ${lock} || {
          echo "goCard: go.sum changed; update the Card lock and hash" >&2
          exit 1
        }
      '';
      proxy =
        if lock == null then
          "off"
        else
          "file://${
            (pkgs.buildGoModule {
              name = "${name}-deps-${
                builtins.substring 0 16 (
                  builtins.hashString "sha256" (
                    builtins.hashFile "sha256" "${src}/go.mod" + builtins.hashFile "sha256" lock
                  )
                )
              }";
              src = pkgs.runCommand "tsugumi-go-lock" { } ''
                mkdir -p "$out/tsugumi"
                cp ${src}/go.mod "$out/go.mod"
                cp ${lock} "$out/go.sum"
                cp ${sdk}/go.mod "$out/tsugumi/go.mod"
              '';
              vendorHash = hash;
              proxyVendor = true;
              modPostBuild = checkLock;
            }).goModules
          }";
    in
    pkgs.runCommandCC name
      {
        nativeBuildInputs = [
          pkgs.go
        ]
        ++ nativeBuildInputs;
        buildInputs = [ pkgs.flatbuffers ] ++ buildInputs;
        env = {
          inherit (pkgs.go) GOOS GOARCH;
          CGO_ENABLED = "1";
          GOTOOLCHAIN = "local";
          GOWORK = "off";
          GOSUMDB = "off";
          GOPROXY = proxy;
        };
      }
      ''
        mkdir -p card "$out/lib"
        cp -r --no-preserve=mode ${src}/. card/
        cd card
        if [ -e tsugumi_adapter.go ]; then
          echo "goCard: tsugumi_adapter.go is reserved for the generated SDK" >&2
          exit 1
        fi
        # Replace any development copy or symlink with the SDK for this contract.
        rm -rf tsugumi
        ln -s ${generatedSdk} tsugumi
        cp ${sdk}/adapter.go.in tsugumi_adapter.go
        ${lib.optionalString (lock != null) "install -m 0644 ${lock} go.sum"}
        export GOCACHE="$TMPDIR/go-cache" GOMODCACHE="$TMPDIR/go-modules"

        ${
          if isDarwin then
            ''
              payload_link="-Wl,-u,_tsugumi_go_entry -Wl,-exported_symbol,_tsugumi_go_entry -Wl,-undefined,error"
              loader_link=(-Wl,-exported_symbol,_tsugumi_main -Wl,-undefined,error)
            ''
          else
            ''
              printf '{ global: tsugumi_go_entry; local: *; };\n' > "$TMPDIR/payload.exports"
              printf '{ global: tsugumi_main; local: *; };\n' > "$TMPDIR/loader.exports"
              payload_link="-Wl,-u,tsugumi_go_entry -Wl,--version-script,$TMPDIR/payload.exports -Wl,-z,defs -Wl,-z,nodelete"
              loader_link=(-Wl,--version-script,"$TMPDIR/loader.exports" -Wl,-z,defs -ldl)
            ''
        }

        # Let Go carry native linker flags from every cgo package to the shared library.
        # The loader looks up the entry dynamically, so -u pulls it from the
        # SDK's archive even though no Go code references it directly.
        go build -mod=readonly -trimpath -buildmode=c-shared \
          -ldflags="-w -buildid= -linkmode=external -extldflags '$payload_link'" \
          -o "$TMPDIR/go-card.so" .
        install -m 755 "$TMPDIR/go-card.so" "$out/lib/go-card.so"
        ${checkLock}

        ${nativeFlags}
        $CXX "''${cxx_flags[@]}" -shared -I${runtime}/include -I${../sdk/cpp/src} \
          ${sdk}/loader.cc ${runtime}/lib/libtsugumi_abi9.a -pthread "''${loader_link[@]}" \
          -o "$out/lib/main.so"
      '';
in
{
  goCard = mkGoCard;
  goCardSdk = mkGoCardSdk;
}
