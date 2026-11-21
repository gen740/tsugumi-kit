{
  description = "Default language SDKs for Tsugumi Cards";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/1c89083e451e891152365b799595cf7790a43325";
  inputs.flake-parts = {
    url = "github:hercules-ci/flake-parts/ea2630b7470978622c7689a1ff75d2b389175399";
    inputs.nixpkgs-lib.follows = "nixpkgs";
  };

  outputs =
    inputs:
    inputs.flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [
        "aarch64-darwin"
        "aarch64-linux"
        "x86_64-linux"
      ];
      imports = [
        ./nix/outputs.nix
        ./nix/r.nix
        ./nix/checks.nix
        ./nix/csharp.nix
        ./nix/go.nix
        ./nix/python.nix
        ./nix/ruby.nix
      ];
    };
}
