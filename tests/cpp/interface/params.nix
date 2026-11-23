{
  mode = 0;
  fd = 0;
  enabled = true;
  gain = 1.0;
  bands = [
    1
    2
  ];
  labels = [
    "a"
    "b"
  ];
  weights = [ 0.5 ];
  toggles = [
    false
    true
  ];
  title = "Example";
  cxxflags = [
    "-DABI_TEST_CARD"
    "-lz"
  ];
}
