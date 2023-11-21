#pragma once

#include <memory>
#include <safetyhook.hpp>

class Pack2FileSystemCreationHook {
public:
    Pack2FileSystemCreationHook();

private:
    static void* pack2_file_system_creation_hook(void* a1, void* a2, void* a3, void* a4);

    SafetyHookInline m_hook{};
};

extern std::unique_ptr<Pack2FileSystemCreationHook> g_pack2_file_system_creation_hook;