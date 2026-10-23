#pragma once

#include <stdint.h>

namespace lep
{
    // Matches the legacy export lookup: rotate left 13, then XOR each byte.
    // Literal names can also be used in switch labels and constant initializers.
    constexpr uint32_t HashExportName(const char* name)
    {
        uint32_t hash = 0;
        while (*name != '\0')
        {
            hash = ((hash << 13) | (hash >> 19)) ^ static_cast<unsigned char>(*name++);
        }
        return hash;
    }
}
