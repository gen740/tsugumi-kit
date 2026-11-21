#include <unistd.h>

unsigned int julia_test_alarm(unsigned int seconds) {
  // Cold startup includes Julia's first precompile of JSON.jl and Parsers.jl.
  return alarm(seconds * 3);
}
