{ self, ... }:
{
  perSystem =
    { pkgs, system, ... }:
    let
      kit = self.lib.${system};
      schemas = pkgs.runCommand "tsugumi-csharp-note-schemas" { } ''
        mkdir -p "$out"
        printf 'namespace inputs; table Inputs {} root_type Inputs;\n' > "$out/_inputs.fbs"
        printf 'namespace parameters; table Params { text:string (id: 0); } root_type Params;\n' > "$out/_params.fbs"
        {
          printf 'namespace outputs;\n'
          cat ${../templates/csharp-write-note/note/_outputs.fbs}
        } > "$out/_outputs.fbs"
      '';
      note = import ../templates/csharp-write-note/note {
        inherit pkgs schemas;
        inputs.tsugumi = kit;
        params = { };
      };
    in
    {
      checks.csharpAbi = pkgs.callPackage ../tests/csharp/check.nix { inherit kit; };
      checks.csharpWriteNoteTemplate = pkgs.runCommand "tsugumi-csharp-note-template-check" { } ''
        test -s ${note.run}/lib/main.so
        test -s ${note.run}/share/tsugumi/api/Card.g.cs
        touch "$out"
      '';
    };
}
