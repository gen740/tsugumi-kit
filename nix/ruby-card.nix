{
  pkgs,
  runtime,
  portCodegen,
}:
let
  inherit (pkgs) lib;
  rubyCardFor =
    {
      src,
      schemas,
      name ? "tsugumi-ruby-card",
      ruby ? pkgs.ruby,
      sourcePrefix ? baseNameOf (toString src),
      lock ? null,
      hash ? null,
      buildInputs ? [ ],
      tools ? [ ],
    }:
    assert lib.assertMsg (
      (lock == null) == (hash == null)
    ) "rubyCard: supply both lock and hash for a locked bundle";
    assert lib.assertMsg (
      sourcePrefix == ""
      || lib.all (part: part != "" && part != "." && part != "..") (lib.splitString "/" sourcePrefix)
    ) "rubyCard: sourcePrefix must be a relative directory inside the Card definition";
    let
      bundleEnvironment = ''
        export HOME="$TMPDIR" BUNDLE_APP_CONFIG="$TMPDIR/bundle-config"
        export BUNDLE_FROZEN=true BUNDLE_FORCE_RUBY_PLATFORM=true BUNDLE_DISABLE_VERSION_CHECK=true
        export BUNDLE_VERSION=system
      '';
      gemfile = lib.path.append (dirOf lock) "Gemfile";
      lockKey = builtins.substring 0 16 (
        builtins.hashString "sha256" (builtins.hashFile "sha256" lock + builtins.hashFile "sha256" gemfile)
      );
      gemSources = pkgs.stdenvNoCC.mkDerivation {
        name = "${name}-gem-sources-${lockKey}";
        impureEnvVars = lib.fetchers.proxyImpureEnvVars;
        SSL_CERT_FILE = "${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt";
        nativeBuildInputs = [ ruby ];
        dontUnpack = true;
        buildPhase = ''
          ${bundleEnvironment}
          cp ${lib.escapeShellArg "${gemfile}"} Gemfile
          cp ${lib.escapeShellArg "${lock}"} Gemfile.lock
          bundle cache --no-install
        '';
        installPhase = ''
          mkdir -p "$out"
          find vendor/cache -maxdepth 1 -name '*.gem' -exec cp {} "$out/" \;
        '';
        outputHashMode = "recursive";
        outputHashAlgo = "sha256";
        outputHash = hash;
      };
      gems = pkgs.stdenv.mkDerivation {
        name = "${name}-gems";
        nativeBuildInputs = [
          pkgs.pkg-config
          ruby
        ];
        inherit buildInputs;
        dontUnpack = true;
        buildPhase = ''
          ${bundleEnvironment}
          mkdir -p "$out/vendor"
          cp ${lib.escapeShellArg "${gemfile}"} "$out/Gemfile"
          cp ${lib.escapeShellArg "${lock}"} "$out/Gemfile.lock"
          ln -s ${gemSources} "$out/vendor/cache"
          export BUNDLE_GEMFILE="$out/Gemfile" BUNDLE_PATH="$out/installed"
          bundle install --local
          # Resolve the bundle during the build so the embedded VM never needs
          # to run Bundler's process-wide setup.
          bundle exec ruby -e '
            puts "Gem.use_paths(#{Gem.dir.dump})"
            Bundler.load.requested_specs.each do |spec|
              puts "Gem::Specification.load(#{spec.loaded_from.dump}).activate"
            end
          ' > "$out/activate.rb"
          rm -rf "$out/vendor" "$out"/installed/ruby/*/{cache,doc}
        '';
        dontInstall = true;
      };
      sdk = ../sdk/ruby;
      cpp = ../sdk/cpp;
      generated =
        pkgs.runCommand "${name}-api"
          {
            nativeBuildInputs = [
              pkgs.flatbuffers
              (pkgs.python3.withPackages (_: [ portCodegen ]))
            ];
          }
          ''
            python3 ${sdk}/card_ruby.py ${cpp} ${lib.escapeShellArg "${schemas}"} "$out" \
              ${pkgs.flatbuffers.src}/reflection/reflection.fbs
          '';
      define = key: value: lib.escapeShellArg "-DTSUGUMI_${key}=${builtins.toJSON value}";
    in
    pkgs.runCommandCC name
      {
        passthru = { inherit generated; };
        nativeBuildInputs = [
          pkgs.pkg-config
          ruby
        ];
        buildInputs = [
          pkgs.flatbuffers
          pkgs.nlohmann_json
          ruby
        ]
        ++ buildInputs;
      }
      ''
        test -f ${lib.escapeShellArg "${src}/main.rb"}
        mkdir -p "$out/share/tsugumi/api"
        cp ${generated}/api/* "$out/share/tsugumi/api/"

        libruby=$(ruby -rrbconfig -e 'print File.join(RbConfig::CONFIG.fetch("libdir"), RbConfig::CONFIG.fetch("LIBRUBY_SO"))')
        read -r -a rubyIncludes <<< "$(pkg-config --cflags ruby-${ruby.version.majMin})"
        read -r -a rubyLibs <<< "$(pkg-config --libs ruby-${ruby.version.majMin})"
        # Ruby's headers contain inline API shims with unused parameters.
        $CC -std=c11 -D_GNU_SOURCE -O2 -fPIC -fvisibility=hidden \
          -Wall -Wextra -Werror -Wno-unused-parameter "''${rubyIncludes[@]}" \
          "-DTSUGUMI_LIBRUBY=\"$libruby\"" \
          ${define "RUBY_CARD" "${src}"} \
          ${define "RUBY_SDK" "${sdk}/tsugumi.rb"} \
          ${define "RUBY_GEMS" "${ruby}/${ruby.gemPath}"} \
          ${define "RUBY_ACTIVATE" (lib.optionalString (lock != null) "${gems}/activate.rb")} \
          ${define "RUBY_SOURCE_PREFIX" (lib.optionalString (sourcePrefix != "") "${sourcePrefix}/")} \
          -c ${sdk}/embed.c -o embed.o

        ${
          if pkgs.stdenv.hostPlatform.isDarwin then
            ''
              link=(-Wl,-exported_symbol,_tsugumi_main -Wl,-undefined,error)
            ''
          else
            ''
              printf '{ global: tsugumi_main; local: *; };\n' > exports
              link=(-Wl,--version-script,exports -Wl,-z,defs -ldl)
            ''
        }
        mkdir -p "$out/lib"
        ${import ./prepare-tools.nix { inherit lib; } tools}
        $CXX -std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
          -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough \
          -shared "''${toolPathObject[@]}" "''${link[@]}" -I${generated}/include -I${runtime}/include \
          -I${cpp}/src -I${sdk} ${generated}/src/card_glue.cc ${sdk}/adapter.cc ${sdk}/json_codec.cc embed.o \
          ${runtime}/lib/libtsugumi_abi9.a "''${rubyLibs[@]}" -pthread -o "$out/lib/main.so"
      '';
in
rubyCardFor
