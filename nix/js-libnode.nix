{
  nodejs-slim_24,
}:
nodejs-slim_24.overrideAttrs (old: {
  # nixpkgs builds an executable by default. The ABI adapter links libnode.
  configureFlags = old.configureFlags ++ [ "--shared" ];
  # The kit exercises the embedding through its own ABI check.
  doCheck = false;
})
