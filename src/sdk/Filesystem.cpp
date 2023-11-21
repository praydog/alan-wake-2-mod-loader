#include <utility/RTTI.hpp>
#include <utility/Scan.hpp>
#include <utility/Module.hpp>
#include <spdlog/spdlog.h>

#include "Filesystem.hpp"

namespace sdk {
std::optional<uintptr_t*> get_global_filesystem() {
    static auto result = []() -> std::optional<uintptr_t*> {
        SPDLOG_INFO("[get_global_filesystem] Searching for global filesystem...");
        // search for Pack2Filesystem, and then the WinFileSystem.
        const auto game = utility::get_executable();
        const auto result1 = utility::rtti::find_object_ptr(game, "class r::GlobalFileSystem<class r::Pack2FileSystem>");

        if (!result1) {
            SPDLOG_INFO("[get_global_filesystem] Failed to find Pack2Filesystem, trying WinFileSystem...");
            const auto result2 = utility::rtti::find_object_ptr(game, "class r::GlobalFileSystem<class r::WinFileSystem>");

            if (!result2) {
                SPDLOG_ERROR("[get_global_filesystem] Failed to find WinFileSystem!");
                return std::nullopt;
            }

            SPDLOG_INFO("[get_global_filesystem] Found WinFileSystem at 0x{:X}", (uintptr_t)*result2);
            return result2;
        }

        SPDLOG_INFO("[get_global_filesystem] Found Pack2Filesystem at 0x{:X}", (uintptr_t)*result1);

        return result1;
    }();

    return result;
}
}