{ inputs, ... }:
{
  run = inputs.tsugumi.cppCard { src = ./src; };
}
