#pragma once

#include <memory>
#include <mutex>
#include <unordered_set>

#include <utility/PointerHook.hpp>
#include <safetyhook.hpp>

#include <sdk/RemedyString.hpp>
#include <sdk/LoaderAdaptor.hpp>

class LooseFileHook {
public:
    LooseFileHook();
    virtual ~LooseFileHook();

private:
    // This function is called in a few places and is generally the main thing used to determine whether it's loaded from disk or not
    // however it's much more complicated than that which is why we have a bunch of other hooks
    bool should_use_loose_file(sdk::RemedyString* path);

    static uint64_t should_use_loose_file_hook(sdk::RemedyString* path, void* rdx, void* r8, void* r9);
    static bool pack2_should_load_file(sdk::LoaderAdaptor* rcx, sdk::RemedyString* filepath, uint64_t r8, void* r9);
    static void* pack2_load_file(sdk::LoaderAdaptor* rcx, sdk::RemedyString* filepath, uint64_t* r8, void* r9);
    static bool filesystem_should_load_file(sdk::LoaderAdaptor* rcx, sdk::RemedyString* filepath, uint64_t r8, void* r9);
    static bool hotload_cache_should_load_file(sdk::LoaderAdaptor* rcx, sdk::RemedyString* filepath, void* r8, void* r9);

    SafetyHookInline m_should_use_loose_file_hook{};
    std::unique_ptr<PointerHook> m_pack2_should_load_file_hook{};
    std::unique_ptr<PointerHook> m_pack2_load_file_hook{};
    std::unique_ptr<PointerHook> m_filesystem_should_load_file_hook{};
    std::unique_ptr<PointerHook> m_hotload_cache_should_load_file_hook{};

    sdk::LoaderAdaptor* m_last_pack2_adaptor{nullptr};
    sdk::LoaderAdaptor* m_last_filesystem_adaptor{nullptr};
    sdk::LoaderAdaptor* m_last_hotload_cache_adaptor{nullptr};

    // Seems to be highly needed to stop the game from locking up.
    // Will change to a value that will make the
    // filesystem_should_load_file return true at some point
    // after a few pack2_load_file calls
    uint64_t m_last_pack2_r8{1};
};

extern std::unique_ptr<LooseFileHook> g_loose_file_hook;