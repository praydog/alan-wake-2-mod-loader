#pragma once

#include <optional>

namespace sdk {
// Windows filesystem is at +0x40 or something in the Pack2 file system but lazy to map it out
std::optional<uintptr_t*> get_global_filesystem();
}