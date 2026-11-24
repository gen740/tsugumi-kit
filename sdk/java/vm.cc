#include <jni.h>

#include <cstdlib>
#include <iterator>
#include <string>

namespace {

auto CreateVm() -> JavaVM* {
  JavaVM* vm = nullptr;
  jsize count = 0;
  if (JNI_GetCreatedJavaVMs(&vm, 1, &count) != JNI_OK) return nullptr;
  if (count != 0) return vm;

  const char* scratch = std::getenv("TMPDIR");
  const std::string temporary =
      std::string("-Djava.io.tmpdir=") + (scratch && *scratch ? scratch : ".");
  const char* home_path = std::getenv("HOME");
  const std::string home =
      std::string("-Duser.home=") + (home_path && *home_path ? home_path : ".");
  JavaVMOption options[] = {
      {const_cast<char*>("-Djava.awt.headless=true"), nullptr},
      {const_cast<char*>("-Xrs"), nullptr},
      {const_cast<char*>("-XX:-UsePerfData"), nullptr},
      {const_cast<char*>(temporary.c_str()), nullptr},
      {const_cast<char*>(home.c_str()), nullptr},
  };
  JavaVMInitArgs args{JNI_VERSION_1_8, std::size(options), options, JNI_FALSE};
  void* env = nullptr;
  if (JNI_CreateJavaVM(&vm, &env, &args) != JNI_OK) return nullptr;
  vm->DetachCurrentThread();
  return vm;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) auto tsugumi_java_vm()
    -> JavaVM* {
  // Cards from the same JDK use the same shared library and VM.
  static auto* const vm = CreateVm();
  return vm;
}
