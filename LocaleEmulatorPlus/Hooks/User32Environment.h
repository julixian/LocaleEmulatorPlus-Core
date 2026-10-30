#pragma once

#include "../support/native/Declarations.h"

namespace lep
{
    struct User32Environment
    {
        PBYTE* Slot;
        PBYTE Source;
        PBYTE Shadow;
        SIZE_T Size;
        RTL_CRITICAL_SECTION Lock;
        BOOL LockReady;
        BOOL Dbcs;
        BOOL Stopped;

        BOOL Initialize(PVOID Module, BOOL TargetDbcs);
        VOID Synchronize();
        VOID Restore();
    };
}
