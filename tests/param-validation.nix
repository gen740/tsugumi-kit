{ pkgs }:
let
  cardInterface = import ../nix/card-interface.nix { inherit pkgs; };
  accepts =
    params:
    (builtins.tryEval
      (cardInterface {
        inputs = ./cpp/interface/_inputs.fbs;
        outputs = ./cpp/interface/_outputs.fbs;
        inherit params;
      }).drvPath
    ).success;
in
accepts { }
&& accepts {
  flag = false;
  count = -3;
  scale = 0.5;
  labels = [
    "x"
    "y"
  ];
  run = 7;
  build = true;
}
&& builtins.all (params: !(accepts params)) [
  [ ]
  null
  ./cpp/interface/params.nix
  { run.x = 1; }
  { build.x = 2; }
  { value = null; }
  { value = { }; }
  { value = [ ]; }
  {
    value = [
      1
      1.0
    ];
  }
  { value = [ [ 1 ] ]; }
  { value = /example; }
  { value = x: x; }
]
