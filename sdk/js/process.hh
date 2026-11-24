#pragma once

#include <node.h>

namespace tsugumi::abi {

using NodeStop = void (*)(void*);
using NodeJoin = bool (*)(void*);

__attribute__((visibility("default"))) auto NodeProcess()
    -> node::InitializationResult&;
__attribute__((visibility("default"))) auto RegisterNodeEnvironment(
    void* context, NodeStop stop, NodeJoin join) -> void;

}  // namespace tsugumi::abi
