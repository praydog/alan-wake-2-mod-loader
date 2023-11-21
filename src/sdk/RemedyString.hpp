#pragma once

#include <cstdint>

namespace sdk {
struct RemedyString {
    char* data;
    uint32_t length; // @ 8
};
}