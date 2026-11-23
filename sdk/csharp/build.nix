{
  pkgs,
  cppRuntime,
  portCodegen,
  paramValues,
  cardInterface,
  publishApi,
}:
let
  inherit (pkgs) lib;
  generate =
    {
      inputs ? null,
      outputs,
      params ? { },
    }:
    let
      interface = cardInterface { inherit inputs outputs params; };
    in
    pkgs.runCommand "tsugumi-csharp-card-bindings"
      {
        nativeBuildInputs = [
          pkgs.flatbuffers
          (pkgs.python3.withPackages (_: [ portCodegen ]))
        ];
      }
      ''
        python3 ${./card_csharp.py} ${lib.escapeShellArg "${interface}"} \
          ${pkgs.flatbuffers.src}/reflection/reflection.fbs "$out"
      '';
  csharpCard =
    {
      src,
      inputs ? null,
      outputs,
      params ? { },
      name ? "tsugumi-csharp-card",
      dotnet ? pkgs.dotnet-sdk_10,
      lock ? null,
      hash ? null,
      tools ? [ ],
    }:
    let
      inherit (pkgs.stdenv.hostPlatform) isDarwin;
      inherit (dotnet) runtime;
      framework = "net${lib.versions.majorMinor runtime.version}";
      os = if isDarwin then "osx" else "linux";
      arch = if pkgs.stdenv.hostPlatform.isAarch64 then "arm64" else "x64";
      rid = "${os}-${arch}";
      sdkRoot = "${dotnet}/share/dotnet";
      dotnetRoot = "${runtime}/share/dotnet";
      hostHeaders = "${sdkRoot}/packs/Microsoft.NETCore.App.Host.${rid}/${runtime.version}/runtimes/${rid}/native";
      hostfxr = "${dotnetRoot}/host/fxr/${runtime.version}/libhostfxr${pkgs.stdenv.hostPlatform.extensions.sharedLibrary}";
      nuget = import ./nuget.nix {
        inherit
          pkgs
          name
          framework
          lock
          hash
          ;
      };
      generated = generate { inherit inputs outputs params; };
      interface = cardInterface { inherit inputs outputs params; };
      managed =
        pkgs.runCommand "${name}-managed"
          {
            nativeBuildInputs = [
              dotnet
              (pkgs.python3.withPackages (_: [ portCodegen ]))
              pkgs.flatbuffers
            ];
            DOTNET_CLI_TELEMETRY_OPTOUT = "1";
            DOTNET_NOLOGO = "1";
            DOTNET_GENERATE_ASPNET_CERTIFICATE = "false";
            # Offline restore checks each locked package's content hash.
            DOTNET_NUGET_SIGNATURE_VERIFICATION = "false";
          }
          ''
            export HOME="$TMPDIR/home" DOTNET_CLI_HOME="$TMPDIR/home"
            export NUGET_PACKAGES="$TMPDIR/packages" NUGET_HTTP_CACHE_PATH="$TMPDIR/http-cache"
            unset NUGET_FALLBACK_PACKAGES

            mkdir -p project/src project/Sdk/Generated project/Sdk/FlatBuffers
            cd project
            cp -r --no-preserve=mode ${lib.escapeShellArg "${src}"}/. src/
            cp ${./Entry.cs} Sdk/Entry.cs
            cp ${./Effects.cs} Sdk/Effects.cs
            cp ${./Artifacts.cs} Sdk/Artifacts.cs
            cp ${pkgs.flatbuffers.src}/net/FlatBuffers/*.cs Sdk/FlatBuffers/
            # The vendored runtime predates nullable reference type annotations.
            sed -i '1s/^\xef\xbb\xbf//; 1i #nullable disable' Sdk/FlatBuffers/*.cs
            # Vtable positions are 32-bit buffer offsets. Only their entries are 16-bit.
            substituteInPlace Sdk/FlatBuffers/FlatBufferVerify.cs --replace-fail \
              'short vtable = Convert.ToInt16(pos - ReadSOffsetT(verifier_buffer, pos));' \
              'int vtable = pos - ReadSOffsetT(verifier_buffer, pos);'
            # The pinned managed verifier uses null for no file identifier.
            # Its nested-buffer caller passes an empty string instead.
            sed -i 's/CheckBufferFromStart("", 0, verifyAction)/CheckBufferFromStart(null, 0, verifyAction)/' \
              Sdk/FlatBuffers/FlatBufferVerify.cs
            cp -r --no-preserve=mode ${generated}/Ports ${generated}/Card.g.cs Sdk/Generated/
            python3 ${./card_csharp.py} embed "${interface}" \
              ${pkgs.flatbuffers.src}/reflection/reflection.fbs "${paramValues params}" Sdk/Generated/EmbeddedParams.g.cs
            ${lib.optionalString (lock != null) "cp ${lib.escapeShellArg "${lock}"} packages.lock.json"}

            cat > Card.csproj <<'PROJECT'
            <Project Sdk="Microsoft.NET.Sdk">
              <PropertyGroup>
                <TargetFramework>${framework}</TargetFramework>
                <AssemblyName>Card</AssemblyName>
                <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
                <GenerateRuntimeConfigurationFiles>true</GenerateRuntimeConfigurationFiles>
                <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
                <TreatWarningsAsErrors>true</TreatWarningsAsErrors>
                <Nullable>enable</Nullable>
                <NuGetAudit>false</NuGetAudit>
                <DebugType>none</DebugType>
              </PropertyGroup>
              <ItemGroup>
                <Compile Include="src/**/*.cs" Exclude="src/**/obj/**/*;src/**/bin/**/*" />
                <Compile Include="Sdk/**/*.cs" />
                ${nuget.references}
              </ItemGroup>
            </Project>
            PROJECT

            dotnet restore Card.csproj --configfile ${nuget.config} ${
              lib.optionalString (lock != null) "--locked-mode"
            }
            dotnet publish Card.csproj --no-restore --configuration Release --output "$out" --tl:off \
              -p:Deterministic=true -p:PathMap="$PWD=/card" \
              -p:PublishReferencesDocumentationFiles=false
          '';
    in
    pkgs.runCommandCC name
      {
        buildInputs = [ pkgs.flatbuffers ];
        passthru = {
          inherit generated managed;
        }
        // lib.optionalAttrs (lock != null) { nugetPackages = nuget.packages; };
      }
      ''
        mkdir -p "$out/lib"
        ${
          if isDarwin then
            ''
              link=(-Wl,-exported_symbol,_tsugumi_main -Wl,-undefined,error)
            ''
          else
            ''
              printf '{ global: tsugumi_main; local: *; };\n' > "$TMPDIR/exports"
              link=(-Wl,--version-script,"$TMPDIR/exports" -Wl,-z,defs -ldl)
            ''
        }
        ${import ../../nix/prepare-tools.nix { inherit lib; } tools}
        $CXX -std=c++2c -O2 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
          -Wall -Wextra -Werror -Wformat=2 -Wimplicit-fallthrough -shared "''${toolPathObject[@]}" \
          -I"${cppRuntime}/include" -I"${../cpp/src}" -isystem "${hostHeaders}" \
          -DTSUGUMI_HOSTFXR='"'${hostfxr}'"' -DTSUGUMI_DOTNET_ROOT='"'${dotnetRoot}'"' \
          -DTSUGUMI_CSHARP_ASSEMBLY='"'${managed}/Card.dll'"' \
          -DTSUGUMI_CSHARP_RUNTIMECONFIG='"'${managed}/Card.runtimeconfig.json'"' \
          ${./host.cc} "${cppRuntime}/lib/libtsugumi_abi.a" -pthread "''${link[@]}" \
          -o "$out/lib/main.so"
      '';
  csharpApi =
    args:
    let
      generated = generate args;
    in
    publishApi "csharp" ''
      cp -r "${generated}/." "$api/"
      cp ${./Effects.cs} ${./Artifacts.cs} "$api/"
    '';
in
{
  inherit csharpCard csharpApi;
}
