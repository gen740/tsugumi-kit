{ pkgs }:
let
  cardInterface = import ../nix/card-interface.nix { inherit pkgs; };
  accepts =
    text:
    (builtins.tryEval
      (cardInterface {
        inputs = { };
        outputs = ./cpp/interface/_outputs.fbs;
        params = builtins.toFile "params.nix" text;
      }).drvPath
    ).success;
in
accepts ''{ run = { flag = false; count = -3; scale = 0.5; labels = [ "x" "y" ]; }; }''
&& builtins.all (text: !(accepts text)) [
  "[]"
  "{ other = {}; }"
  "{ run = null; }"
  "{ run.x = 1; build.x = 2; }"
  "{ run.x = []; }"
  "{ run.x = [1 1.0]; }"
  "{ run.x = [[1]]; }"
  "{ run.x = /example; }"
  "{ build.x = {}; }"
]
