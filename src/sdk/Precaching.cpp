#include <utility/RTTI.hpp>
#include <utility/Scan.hpp>
#include <utility/Module.hpp>
#include <spdlog/spdlog.h>

#include "Filesystem.hpp"
#include "Precaching.hpp"

namespace sdk {
std::optional<uintptr_t> no_texture_ref{};

void update_no_texture_ref() {
    if (no_texture_ref.has_value()) {
        return;
    }

    const auto game = utility::get_executable();
    const auto str = utility::scan_string(game, "textures\\helpers\\notexture.tex");

    if (!str) {
        SPDLOG_ERROR("[update_no_texture_ref] Failed to find string ref!");
        return;
    }

    const auto ref = utility::scan_displacement_reference(game, *str);

    if (!ref) {
        SPDLOG_ERROR("[update_no_texture_ref] Failed to find reference!");
        return;
    }

    SPDLOG_INFO("[update_no_texture_ref] Found reference at 0x{:X}", *ref);

    no_texture_ref = ref;
}

std::optional<uintptr_t> get_file_precache_global() {
    static auto result = []() -> std::optional<uintptr_t> {
        SPDLOG_INFO("[get_file_precache_global] Searching for file precache global...");
        update_no_texture_ref();

        if (!no_texture_ref) {
            return std::nullopt;
        }

        auto disasm_behind = utility::get_disassembly_behind(*no_texture_ref);

        // reverse the vector
        std::reverse(disasm_behind.begin(), disasm_behind.end());

        // Find first displacement that is not a call.
        for (const auto& entry : disasm_behind) {
            if (!std::string_view{entry.instrux.Mnemonic}.starts_with("CALL")) {
                const auto disp = utility::resolve_displacement(entry.addr);

                if (disp) {
                    SPDLOG_INFO("[get_file_precache_global] Found file precache global at 0x{:X}", *disp);
                    return *disp;
                }
            }
        }

        SPDLOG_ERROR("[get_file_precache_global] Failed to find file precache global!");

        return std::nullopt;
    }();

    return result;
}

std::optional<uintptr_t> get_file_precache_function() {
    static auto result = []() -> std::optional<uintptr_t> {
        SPDLOG_INFO("[get_file_precache_function] Searching for file precache function...");
        update_no_texture_ref();

        if (!no_texture_ref) {
            return std::nullopt;
        }

        auto disasm_behind = utility::get_disassembly_behind(*no_texture_ref);

        // reverse the vector
        std::reverse(disasm_behind.begin(), disasm_behind.end());

        // Find first call.
        for (const auto& entry : disasm_behind) {
            if (std::string_view{entry.instrux.Mnemonic}.starts_with("CALL")) {
                const auto disp = utility::resolve_displacement(entry.addr);

                if (disp) {
                    SPDLOG_INFO("[get_file_precache_function] Found file precache function at 0x{:X}", *disp);
                    return *disp;
                }
            }
        }

        SPDLOG_ERROR("[get_file_precache_function] Failed to find file precache function!");

        return std::nullopt;
    }();

    return result;
}

struct FilePrecacheThing {
    FilePrecacheThing() {
        const auto vtables = utility::rtti::find_vtables(utility::get_executable(), "class coregame::FilePrecache");

        if (vtables.empty()) {
            SPDLOG_ERROR("[FilePrecacheThing] Failed to find vtables!");
            return;
        }

        if (vtables.size() < 2) {
            SPDLOG_ERROR("[FilePrecacheThing] Found less than 2 vtables!");
            return;
        }

        vtable1 = (void*)vtables[0];
        vtable2 = (void*)vtables[1];
    };

    void* vtable1{};
    void* vtable2{};
    char poopypad[0x3000]{};
};

void precache_file(std::string_view file_path) {
    g_inside_precache = true;
    const auto fn = get_file_precache_function();
    const auto global = get_file_precache_global();

    if (!fn || !global) {
        return;
    }

    using Fn = void (*)(void*, const char*);

    const auto fn_ptr = (Fn)*fn;
    void* final_global = nullptr;
    static FilePrecacheThing* our_global = new FilePrecacheThing();

    if (*(void**)*global == nullptr) {
        SPDLOG_ERROR("[precache_file] File precache global is null, creating our own!");
        //*(void**)*global = our_global;
        final_global = our_global;
    } else {
        final_global = *(void**)*global;
    }
    
    const auto global_fs_ptr = get_global_filesystem();
    
    if (!global_fs_ptr) {
        return;
    }

    auto global_fs = **global_fs_ptr;

    if (global_fs == 0) {
        SPDLOG_ERROR("[precache_file] Global filesystem is null!");
        return;
    }

    const auto old_vtable = *(void**)global_fs;
    const auto old_object = **global_fs_ptr;

    fn_ptr((void**)final_global, file_path.data());

    g_inside_precache = false;
}
}