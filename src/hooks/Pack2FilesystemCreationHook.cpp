#include <utility/RTTI.hpp>
#include <utility/Scan.hpp>
#include <utility/Module.hpp>
#include <spdlog/spdlog.h>

#include "Pack2FilesystemCreationHook.hpp"

std::unique_ptr<Pack2FileSystemCreationHook> g_pack2_file_system_creation_hook{};

Pack2FileSystemCreationHook::Pack2FileSystemCreationHook() {
    SPDLOG_INFO("[Pack2FileSystemCreationHook] Pack2FileSystemCreationHook constructor called!");

    const auto game = utility::get_executable();
    const auto vtable = utility::rtti::find_vtable(game, "class r::GlobalFileSystem<class r::Pack2FileSystem>");

    if (!vtable) {
        SPDLOG_ERROR("[Pack2FileSystemCreationHook] Failed to find Pack2FileSystem vtable!");
        return;
    }

    SPDLOG_INFO("[Pack2FileSystemCreationHook] Found Pack2FileSystem vtable at 0x{:X}", *vtable);
    const auto vtable_ref = utility::scan_displacement_reference(game, *vtable);

    if (!vtable_ref) {
        SPDLOG_ERROR("[Pack2FileSystemCreationHook] Failed to find Pack2FileSystem vtable reference!");
        return;
    }

    const auto fn = utility::find_function_start_with_call(*vtable_ref);

    if (!fn) {
        SPDLOG_ERROR("[Pack2FileSystemCreationHook] Failed to find Pack2FileSystem creation function!");
        return;
    }

    SPDLOG_INFO("[Pack2FileSystemCreationHook] Found Pack2FileSystem creation function at 0x{:X}", *fn);
    m_hook = safetyhook::create_inline((void*)*fn, (void*)pack2_file_system_creation_hook);

    if (!m_hook) {
        SPDLOG_ERROR("[Pack2FileSystemCreationHook] Failed to hook Pack2FileSystem creation function!");
        return;
    }

    SPDLOG_INFO("[Pack2FileSystemCreationHook] Hooked Pack2FileSystem creation function!");
}

void* Pack2FileSystemCreationHook::pack2_file_system_creation_hook(void* a1, void* a2, void* a3, void* a4) {
    SPDLOG_INFO("[Pack2FileSystemCreationHook] Pack2FileSystem creation called!");

    void* result = g_pack2_file_system_creation_hook->m_hook.unsafe_call<void*>(a1, a2, a3, a4);

    // To be seen if this is necessary or not to precache files
    /*for (const auto& entry : std::filesystem::recursive_directory_iterator("data")) {
        if (entry.is_regular_file() && !entry.path().string().ends_with(".lua") && !entry.path().string().ends_with(".binrfx")) {
            SPDLOG_INFO("[FilePrecacheHook] Precaching file: {}", entry.path().string());
            precache_file(entry.path().string().c_str());
        }
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator("data_pc")) {
        if (entry.is_regular_file() && !entry.path().string().ends_with(".lua") && !entry.path().string().ends_with(".binrfx")) {
            SPDLOG_INFO("[FilePrecacheHook] Precaching file: {}", entry.path().string());
            precache_file(entry.path().string().c_str());
        }
    }*/

    return result;
}