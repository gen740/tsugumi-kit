#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>

/* The worker loads one Card per process. Set PATH before any Card code or
 * embedded language runtime starts. This object is linked only with tools. */
__attribute__((constructor)) static void set_tool_path(void) {
  if (setenv("PATH", TSUGUMI_TOOL_PATH, 1) != 0) {
    perror("Could not set the Card tool PATH");
    abort();
  }
}
