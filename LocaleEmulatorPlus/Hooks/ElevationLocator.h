#pragma once

namespace lep
{
    struct ElevationEntry
    {
        PVOID Routine;
        BOOL RegisterArguments; // x86 LTCG: application/command in ECX/EDX.
    };

    ElevationEntry FindElevationEntry(PVOID Image);
}
