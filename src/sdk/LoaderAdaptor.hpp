#pragma once

#include <cstdint>

namespace sdk {
struct RemedyString;
}

namespace sdk {
class LoaderAdaptor {
public:
    virtual ~LoaderAdaptor() = default;
    virtual bool ShouldLoadFile(RemedyString* filepath, uint64_t unk_val, void* r9) = 0;
    virtual bool LoadFile(RemedyString* filepath, uint64_t* r8, void* r9) = 0;

private:
};
}