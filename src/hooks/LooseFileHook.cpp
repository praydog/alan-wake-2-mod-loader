#include <filesystem>
#include <array>

#include <windows.h>

#include <utility/RTTI.hpp>
#include <utility/Scan.hpp>
#include <utility/Module.hpp>
#include <utility/String.hpp>
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

bool LooseFileHook::should_log_file_pack2(sdk::RemedyString* path) {
    static const auto result = []() {
        const auto cmd_line = GetCommandLineW();
        const auto cmd_line_str = std::wstring(cmd_line);

        return cmd_line_str.find(L"-logpack2") != std::wstring::npos || cmd_line_str.find(L"-logpack2_exts") != std::wstring::npos;
    }();

    // Get the list of requested extensions the user wishes to log (if none then log all)
    static const auto extensions = []() -> std::vector<std::string> {
        const auto cmd_line = GetCommandLineW();
        const auto cmd_line_str = std::wstring(cmd_line);

        const auto extensions_start = cmd_line_str.find(L"-logpack2_exts");

        if (extensions_start == std::wstring::npos) {
            return {};
        }

        const auto exts_str_len = std::string{"-logpack2_exts"}.length();

        // Make sure there's something after the -logpack2_exts
        if (cmd_line_str.length() <= extensions_start + exts_str_len + 1) {
            return {};
        }

        // Now, the exts should be encased in quotes. Find the first quote
        // but it must be before the next "-" (which is the next option)
        const auto first_quote = cmd_line_str.find(L"\"", extensions_start + exts_str_len);
        const auto next_dash = cmd_line_str.find(L"-", extensions_start + exts_str_len);

        if (first_quote != std::wstring::npos) {
            const auto next_quote = cmd_line_str.find(L"\"", first_quote + 1);

            if (next_quote == std::wstring::npos) {
                return {};
            }

            const auto next_dash = cmd_line_str.find(L"-", extensions_start + exts_str_len);

            if (next_dash != std::wstring::npos && next_dash < first_quote) {
                return {};
            }

            auto exts = cmd_line_str.substr(first_quote + 1, next_quote - first_quote - 1);

            std::vector<std::string> result;

            if (exts.find(L",") == std::wstring::npos) {
                result.push_back(utility::narrow(exts));
                return result;
            }

            size_t pos = 0;
            size_t last_pos = 0;
            while (true) {
                pos = exts.find(L",", pos + 1);

                if (pos == std::wstring::npos) {
                    const auto ext = utility::narrow(std::wstring{exts.substr(last_pos, exts.length())});
                    result.push_back(ext);
                    SPDLOG_INFO("[LooseFileHook] Logging pack2 file with extension: {}", ext);
                    break;
                }

                const auto ext = utility::narrow(std::wstring{exts.substr(last_pos, pos)});
                result.push_back(ext);
                last_pos = pos + 1;

                SPDLOG_INFO("[LooseFileHook] Logging pack2 file with extension: {}", ext);
            }

            return result;
        }

        return {};
    }();

    if (!result) {
        return false;
    }

    if (extensions.empty()) {
        return true;
    }
    
    const auto path_view = std::string_view{path->data, path->length};

    for (const auto& ext : extensions) {
        if (path_view.ends_with(ext)) {
            return true;
        }
    }

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

    if (filepath->data == nullptr || filepath->length == 0) {
        return result;
    }

    const auto sv = std::string_view{filepath->data, filepath->length};
    const auto exists = std::filesystem::exists(sv);

    // The filesystem's ShouldLoadFile returns true once it has been loaded successfully by the game's Pack2 loader
    // which means we can go ahead and force the filesystem to load it, otherwise the game locks up.
    const auto file_is_already_loaded = g_loose_file_hook->m_last_filesystem_adaptor != nullptr && g_loose_file_hook->m_last_filesystem_adaptor->ShouldLoadFile(filepath, r8, nullptr);

    // This is the real deal here.
    if (exists && file_is_already_loaded) {
        SPDLOG_INFO("[LooseFileHook] Forcing filesystem to load file {}", sv);
        return false;
    } else if (file_is_already_loaded && should_log_file_pack2(filepath)) {
        SPDLOG_INFO("[LooseFileHook] Game has already loaded via pack: {}", sv);
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