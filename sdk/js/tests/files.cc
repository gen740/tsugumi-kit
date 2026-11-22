#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "js_cases.hh"

#ifdef __APPLE__
#include <mach/mach.h>

auto ThreadCount() -> int {
  thread_act_array_t threads;
  mach_msg_type_number_t count;
  CHECK(task_threads(mach_task_self(), &threads, &count) == KERN_SUCCESS);
  for (mach_msg_type_number_t i = 0; i < count; ++i)
    mach_port_deallocate(mach_task_self(), threads[i]);
  vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                count * sizeof(*threads));
  return static_cast<int>(count);
}
#else
#include <filesystem>
#include <iterator>

auto ThreadCount() -> int {
  const std::filesystem::directory_iterator tasks("/proc/self/task");
  return static_cast<int>(std::distance(begin(tasks), end(tasks)));
}
#endif

// Registered before the Card loads, so it runs after the Card's exit
// handler and libnode's static destructors.
auto CheckStoppedAtExit() -> void {
  const int threads = ThreadCount();
  if (threads == 1) return;
  std::fprintf(stderr, "Node.js left %d threads running at exit\n",
               threads - 1);
  std::_Exit(1);
}

auto main(int argc, char** argv) -> int {
  CHECK(argc == 2);
  alarm(30);
  CHECK(std::atexit(CheckStoppedAtExit) == 0);
  auto* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    std::fprintf(stderr, "%s\n", dlerror());
    return 1;
  }
  const auto entry =
      reinterpret_cast<tsugumi_main_fn>(dlsym(library, "tsugumi_main"));
  CHECK(entry);
  tsugumi_call call = {TSUGUMI_ABI_VERSION, make_input(VALID, 8, -1), nullptr,
                       nullptr};
  tsugumi_bytes result;
  CHECK(entry(&call, &result) == 0);
  check_file_output(result);
  for (int mode = 9; mode <= 15; ++mode) {
    if (mode == 11) continue;
    call.input = make_input(VALID, mode, -1);
    CHECK(entry(&call, &result) == 0);
    check_card_failure(result, mode);
  }

  for (int scenario = MERGE_TEMPERATURE; scenario <= MERGE_EMPTY; ++scenario) {
    call.input = make_merge_input(scenario);
    CHECK(entry(&call, &result) == 0);
    check_merge(result, scenario);
  }

  int displays = 0;
  const tsugumi_effects effects = {
      [](void* host, const char* view, const uint8_t* bytes, size_t size) {
        CHECK(view == std::string_view("undeclared"));
        CHECK(size == 1 && bytes[0] == 42);
        ++*static_cast<int*>(host);
        return 37;
      },
      nullptr,
      nullptr,
  };
  call = {TSUGUMI_ABI_VERSION, make_input(VALID, 11, -1), &displays, &effects};
  CHECK(entry(&call, &result) == 0);
  check_success(result, 0);
  CHECK(displays == 1);

  // A Worker keeps sending after the call. Process exit must stop Node
  // before libnode's static destructors run.
  static std::atomic<int> sent;
  const tsugumi_effects running = {
      [](void*, const char* view, const uint8_t*, size_t) {
        CHECK(view == std::string_view("text"));
        sent.fetch_add(1);
        return 0;
      },
      [](void*) { return 0; },
      [](void*) -> int64_t { return -1; },
  };
  call = {TSUGUMI_ABI_VERSION, make_input(VALID, 16, -1), nullptr, &running};
  CHECK(entry(&call, &result) == 0);
  check_success(result, 0);
  while (sent.load() == 0) usleep(1000);
  CHECK(dlclose(library) == 0);
}
