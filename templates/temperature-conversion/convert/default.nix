{ inputs, ... }:
{
  # Provisional injection of the ABI 9 C++ builder.
  run = inputs.tsugumi.cppCard { src = ./src; };
}
