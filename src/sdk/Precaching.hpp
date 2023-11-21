#pragma once

#include <optional>
#include <string_view>

namespace sdk {
std::optional<uintptr_t> get_file_precache_global();
std::optional<uintptr_t> get_file_precache_function();

static inline thread_local bool g_inside_precache = false;
void precache_file(std::string_view file_path);
}