#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <stacktrace>
#include <array>

#include <windows.h>

#include <utility/Module.hpp>
#include <utility/Scan.hpp>
#include <utility/Patch.hpp>
#include <utility/Thread.hpp>
#include <utility/ScopeGuard.hpp>
#include <utility/RTTI.hpp>
#include <utility/PointerHook.hpp>

#include <safetyhook.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>

#include "sdk/RemedyString.hpp"
#include "sdk/Precaching.hpp"
#include "sdk/Filesystem.hpp"
#include "sdk/LoaderAdaptor.hpp"

#include "hooks/LooseFileHook.hpp"
#include "hooks/Pack2FilesystemCreationHook.hpp"

bool g_already_patched{false};

void startup_thread() {
    AllocConsole();
    freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);

    // Set up spdlog to sink to the console
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] [alan-wake-2-mod-loader] %v");
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);
    spdlog::set_default_logger(spdlog::stdout_logger_mt("console"));

    SPDLOG_INFO("Alan Wake 2 Mod Loader v1.0");
    SPDLOG_INFO("By praydog");

    g_loose_file_hook = std::make_unique<LooseFileHook>();
    g_pack2_file_system_creation_hook = std::make_unique<Pack2FileSystemCreationHook>();

    SPDLOG_INFO("Goodbye!");
}

bool IsUALPresent() {
    for (const auto& entry : std::stacktrace::current()) {
        HMODULE hModule = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)entry.native_handle(), &hModule)) {
            if (GetProcAddress(hModule, "IsUltimateASILoader") != NULL)
                return true;
        }
    }
    return false;
}

extern "C" __declspec(dllexport) void InitializeASI() {
    if (g_already_patched) {
        return;
    }

    g_already_patched = true;
    startup_thread();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        if (!IsUALPresent()) { InitializeASI(); }
    }

    return TRUE;
}