#include <filesystem>
#include <array>

#include <windows.h>

#include <utility/RTTI.hpp>
#include <utility/Scan.hpp>
#include <utility/Module.hpp>
#include <spdlog/spdlog.h>

#include "LooseFileHook.hpp"

std::unique_ptr<LooseFileHook> g_loose_file_hook{};

LooseFileHook::LooseFileHook() {
    SPDLOG_INFO("[LooseFileHook] LooseFileHook constructor called!");
    SPDLOG_INFO("[LooseFileHook] Searching for ShouldUseLooseFile...");
    const auto game = utility::get_executable();
    auto fn = utility::find_function_from_string_ref(game, "data/lualibs");

    if (!fn) {
        SPDLOG_ERROR("[LooseFileHook] Failed to find ShouldUseLooseFile! (no string ref)");
        return;
    }

    const auto rva = *fn - (uintptr_t)game;
    SPDLOG_INFO("[LooseFileHook] Found ShouldUseLooseFile at {:x} (RVA {:x})", *fn, rva);

    m_should_use_loose_file_hook = safetyhook::create_inline((void*)*fn, (void*)should_use_loose_file_hook);

    if (!m_should_use_loose_file_hook) {
        SPDLOG_ERROR("[LooseFileHook] Failed to hook ShouldUseLooseFile!");
        return;
    }

    const auto adaptor_filesystem_vtable = utility::rtti::find_vtable(game, "class r::Loader_Adaptor_FileSystem");

    if (adaptor_filesystem_vtable) {
        SPDLOG_INFO("[LooseFileHook] Found Loader_Adaptor_FileSystem vtable at 0x{:X}", *adaptor_filesystem_vtable);

        auto& should_load_file_fn = ((uintptr_t*)*adaptor_filesystem_vtable)[1];
        SPDLOG_INFO("[LooseFileHook] Found r::Loader_Adaptor_FileSystem::ShouldLoadFile function at 0x{:X}", should_load_file_fn);
        m_filesystem_should_load_file_hook = std::make_unique<PointerHook>((void**)&should_load_file_fn, (void*)filesystem_should_load_file);
    } else {
        SPDLOG_ERROR("[LooseFileHook] Failed to find Loader_Adaptor_FileSystem vtable!");
    }

    const auto adaptor_pack2_vtable = utility::rtti::find_vtable(utility::get_executable(), "class r::Loader_Adaptor_Pack2");

    if (adaptor_pack2_vtable) {
        SPDLOG_INFO("[LooseFileHook] Found Loader_Adaptor_Pack2 vtable at 0x{:X}", *adaptor_pack2_vtable);

        auto& vfunc = ((uintptr_t*)*adaptor_pack2_vtable)[1];
        auto& vfunc_load = ((uintptr_t*)*adaptor_pack2_vtable)[2];
        SPDLOG_INFO("[LooseFileHook] Found r::Loader_Adaptor_Pack2::ShouldLoadFile function at 0x{:X}", vfunc);
        SPDLOG_INFO("[LooseFileHook] Found r::Loader_Adaptor_Pack2::LoadFile function at 0x{:X}", vfunc_load);

        m_pack2_should_load_file_hook = std::make_unique<PointerHook>((void**)&vfunc, (void*)pack2_should_load_file);
        m_pack2_load_file_hook = std::make_unique<PointerHook>((void**)&vfunc_load, (void*)pack2_load_file);
    } else {
        SPDLOG_ERROR("[LooseFileHook] Failed to find Loader_Adaptor_Pack2 vtable!");
    }

    const auto hotload_cache_vtable = utility::rtti::find_vtable(utility::get_executable(), "class r::Loader_Adaptor_HotloadDataCache");

    if (hotload_cache_vtable) {
        auto& hotload_cache_vfunc = ((uintptr_t*)*hotload_cache_vtable)[1];
        m_hotload_cache_should_load_file_hook = std::make_unique<PointerHook>((void**)&hotload_cache_vfunc, (void*)hotload_cache_should_load_file);
    } else {
        SPDLOG_ERROR("[LooseFileHook] Failed to find Loader_Adaptor_HotloadDataCache vtable!");
    }

    SPDLOG_INFO("[LooseFileHook] Finished setting up LooseFileHook!");
}

LooseFileHook::~LooseFileHook() {
    SPDLOG_INFO("[LooseFileHook] LooseFileHook destructor called!"); // yay
}

// This function is called in a few places and is generally the main thing used to determine whether it's loaded from disk or not
// however it's much more complicated than that which is why we have a bunch of other hooks
__forceinline bool LooseFileHook::should_use_loose_file(sdk::RemedyString* path) {
    if (path == nullptr || path->length == 0 || path->data == nullptr) {
        return false;
    }

    const auto str = std::string{ path->data, path->length };

    if (str.ends_with(".meta")) {
        SPDLOG_INFO("[LooseFileHook] .meta file detected: {}", str);
        //return false;
    }

    if (std::filesystem::exists(str)) {
        SPDLOG_INFO("[LooseFileHook] Found modded file: {}", str);

        // This is the real deal here.
        if (m_last_filesystem_adaptor != nullptr && m_last_filesystem_adaptor->ShouldLoadFile(path, m_last_pack2_r8, nullptr)) {
            SPDLOG_INFO("[LooseFileHook] Forcing filesystem to load file {}", str);
            return true;
        }

#ifdef _DEBUG
        std::array<void*, 100> addresses{};
        const auto num_addresses = RtlCaptureStackBackTrace(0, addresses.size(), addresses.data(), nullptr);
        for (size_t i = 0; i < num_addresses; ++i) {
            const auto callstack_rva = (uintptr_t)addresses[i] - (uintptr_t)utility::get_module_within(addresses[i]).value_or(nullptr);
            SPDLOG_INFO("[LooseFileHook]  Callstack[{:x}] = {:x} (RVA {:x})", i, (uintptr_t)addresses[i], callstack_rva);
        }
#endif

        return false;
    } else {
        //SPDLOG_INFO("[LooseFileHook] {}", str);
    }

    // TODO: mess with CWD?
    return false;
}

uint64_t LooseFileHook::should_use_loose_file_hook(sdk::RemedyString* path, void* rdx, void* r8, void* r9) {
    // its a bool but whatever
    uint64_t result = g_loose_file_hook->m_should_use_loose_file_hook.unsafe_call<uint64_t>(path, rdx, r8, r9);

    // well, original func is already returning true... so...
    if (result & 1 != 0) {
        return result;
    }

    return result | (uint64_t)g_loose_file_hook->should_use_loose_file(path);
}

bool LooseFileHook::pack2_should_load_file(sdk::LoaderAdaptor* rcx, sdk::RemedyString* filepath, uint64_t r8, void* r9) {
    g_loose_file_hook->m_last_pack2_adaptor = rcx;
    g_loose_file_hook->m_last_pack2_r8 = r8;
    const auto result = g_loose_file_hook->m_pack2_should_load_file_hook->get_original<decltype(pack2_should_load_file)*>()(rcx, filepath, r8, r9);
    const auto exists = std::filesystem::exists(std::string{filepath->data, filepath->length});

    if (exists && !result && filepath != nullptr && filepath->data != nullptr && filepath->length != 0) {
        //SPDLOG_INFO("[filesystem_should_load_file] Pack2 should load file called! This: {:x} {}", (uintptr_t)rcx, std::string_view{filepath->data, filepath->length});
        return false;
    } else if (result && filepath != nullptr && filepath->data != nullptr && filepath->length != 0) {
        if (exists) {
            //SPDLOG_INFO("[filesystem_should_load_file] Pack2 should load file called! This: {:x} {}", (uintptr_t)rcx, std::string_view{filepath->data, filepath->length});

            if (g_loose_file_hook->m_last_filesystem_adaptor != nullptr && g_loose_file_hook->m_last_filesystem_adaptor->ShouldLoadFile(filepath, r8, r9)) {
                SPDLOG_INFO("[filesystem_should_load_file] Forcing filesystem to load file {}", std::string_view{filepath->data, filepath->length});
                return false;
            }
        }
    }

    return result;
}

void* LooseFileHook::pack2_load_file(sdk::LoaderAdaptor* rcx, sdk::RemedyString* filepath, uint64_t* r8, void* r9) {
    g_loose_file_hook->m_last_pack2_adaptor = rcx;

    //SPDLOG_INFO("[pack2_load_file] Pack2 load file called! This: {:x}", (uintptr_t)rcx);
    const auto result = g_loose_file_hook->m_pack2_load_file_hook->get_original<decltype(pack2_load_file)*>()(rcx, filepath, r8, r9);
    const auto exists = std::filesystem::exists(std::string{filepath->data, filepath->length});

    /*if (exists && filepath != nullptr && filepath->data != nullptr && filepath->length != 0) {
        SPDLOG_INFO("[pack2_load_file] Pack2 load file called! This: {:x} {}", (uintptr_t)rcx, std::string_view{filepath->data, filepath->length});
    }*/

    return result;
}

bool LooseFileHook::filesystem_should_load_file(sdk::LoaderAdaptor* rcx, sdk::RemedyString* filepath, uint64_t r8, void* r9) {
    g_loose_file_hook->m_last_filesystem_adaptor = rcx;

    const auto result = g_loose_file_hook->m_filesystem_should_load_file_hook->get_original<decltype(filesystem_should_load_file)*>()(rcx, filepath, r8, r9);

    /*if (result) {
        if (filepath != nullptr && filepath->data != nullptr && filepath->length != 0) {
            SPDLOG_INFO("[filesystem_should_load_file] Filesystem should load file called! This: {:x} {}", (uintptr_t)rcx, std::string_view{filepath->data, filepath->length});
        }
    }*/

    return result;
}

// unused... for now
bool LooseFileHook::hotload_cache_should_load_file(sdk::LoaderAdaptor* rcx, sdk::RemedyString* filepath, void* r8, void* r9) {
    g_loose_file_hook->m_last_hotload_cache_adaptor = rcx;

    const auto result = g_loose_file_hook->m_hotload_cache_should_load_file_hook->get_original<decltype(hotload_cache_should_load_file)*>()(rcx, filepath, r8, r9);
    
    return result;
}