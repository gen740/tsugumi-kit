# Optional ABI 8-compatible offline NuGet restore.
{
  pkgs,
  name,
  framework,
  lock,
  hash,
}:
let
  inherit (pkgs) lib;
  sections = if lock == null then { } else (builtins.fromJSON (builtins.readFile lock)).dependencies;
  entries = lib.mapAttrsToList (id: entry: entry // { inherit id; });
  direct = builtins.filter (entry: entry.type == "Direct") (entries (sections.${framework} or { }));
  resolved = lib.unique (
    map
      (entry: {
        id = lib.toLower entry.id;
        version = lib.toLower entry.resolved;
      })
      (
        builtins.filter (entry: entry.type != "Project") (
          lib.concatMap entries (builtins.attrValues sections)
        )
      )
  );
  plain = value: builtins.match "[a-z0-9][a-z0-9._+-]*" value != null;
  escapeXml =
    value: builtins.replaceStrings [ "&" "\"" "<" ">" ] [ "&amp;" "&quot;" "&lt;" "&gt;" ] value;

  # Key the fixed-output derivation by the lock as well as its output hash.
  packagesName = "${name}-nuget-lock-${builtins.substring 0 16 (builtins.hashFile "sha256" lock)}";
  packages =
    pkgs.runCommand packagesName
      {
        nativeBuildInputs = [ pkgs.curl ];
        SSL_CERT_FILE = "${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt";
        impureEnvVars = lib.fetchers.proxyImpureEnvVars;
        outputHashMode = "recursive";
        outputHash = hash;
      }
      ''
        mkdir -p "$out"
        ${lib.concatMapStrings (entry: ''
          curl --fail --silent --show-error --location \
            -o "$out/${entry.id}.${entry.version}.nupkg" \
            "https://api.nuget.org/v3-flatcontainer/${entry.id}/${entry.version}/${entry.id}.${entry.version}.nupkg"
        '') resolved}
      '';
in
assert lib.assertMsg (
  (lock == null) == (hash == null)
) "csharpCard: provide lock and hash together (use lib.fakeHash to discover the hash)";
assert lib.assertMsg (
  lock == null || sections ? ${framework}
) "csharpCard: the NuGet lock has no ${framework} section";
assert lib.assertMsg (builtins.all (
  entry: plain entry.id && plain entry.version
) resolved) "csharpCard: the lock contains invalid NuGet IDs or versions";
{
  inherit packages;
  references = lib.concatMapStringsSep "\n" (
    entry:
    ''<PackageReference Include="${escapeXml entry.id}" Version="${escapeXml entry.requested}" />''
  ) direct;
  config = pkgs.writeText "${name}-NuGet.Config" ''
    <?xml version="1.0" encoding="utf-8"?>
    <configuration>
      <packageSources>
        <clear />
        ${lib.optionalString (lock != null) ''<add key="lock" value="${packages}" />''}
      </packageSources>
      <fallbackPackageFolders><clear /></fallbackPackageFolders>
    </configuration>
  '';
}
