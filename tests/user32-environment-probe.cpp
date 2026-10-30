#include "../LocaleEmulatorPlus/Hooks/User32Environment.h"
#include "../LocaleEmulatorPlus/support/Support.h"
#include <stdio.h>

struct ConcurrentCheck
{
    lep::User32Environment* State;
    BOOL (WINAPI* Query)();
    BOOL Expected;
    LONG Failures;
};
static DWORD WINAPI CheckThread(PVOID value)
{
    auto check = static_cast<ConcurrentCheck*>(value);
    for (int i = 0; i != 128; ++i)
    {
        check->State->Synchronize();
        if (check->Query() != check->Expected || GetSystemMetrics(SM_DBCSENABLED) != check->Expected)
            InterlockedIncrement(&check->Failures);
    }
    return 0;
}

int main()
{
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    auto query = (BOOL (WINAPI*)())GetProcAddress(user32, "CheckDBCSEnabledExt");
    if (!query) return 1;
    BOOL host = query();
    int width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN);
    lep::User32Environment unavailable{};
    if (unavailable.Initialize(GetModuleHandleW(L"kernel32.dll"), !host) || unavailable.LockReady)
        return 6;
    lep::User32Environment state{};
    for (int repeat = 0; repeat != 2; ++repeat)
    {
        if (!state.Initialize(user32, !host)) return 2;
        PBYTE original = *state.Slot;
        ULONG flags = *(volatile ULONG*)original;
        state.Synchronize();
        if (!state.Shadow || *state.Slot != state.Shadow || state.Source != original || query() == host ||
            GetSystemMetrics(SM_DBCSENABLED) == host || *(volatile ULONG*)original != flags ||
            (*(ULONG*)state.Shadow & ~2u) != (flags & ~2u)) return 3;
        for (int i = 0; i != 8; ++i)
        {
            state.Synchronize();
            if (GetSystemMetrics(SM_CXSCREEN) != width || GetSystemMetrics(SM_CYSCREEN) != height ||
                query() == host || *(volatile ULONG*)original != flags) return 4;
        }
        ConcurrentCheck concurrent = {&state, query, !host, 0};
        HANDLE threads[4]{};
        for (int i = 0; i != 4; ++i)
            if ((threads[i] = CreateThread(nullptr, 0, CheckThread, &concurrent, 0, nullptr)) == nullptr) return 7;
        if (WaitForMultipleObjects(4, threads, TRUE, 5000) != WAIT_OBJECT_0 || concurrent.Failures != 0) return 8;
        for (HANDLE thread : threads) CloseHandle(thread);
        PBYTE published = state.Shadow;
        state.Restore();
        MEMORY_BASIC_INFORMATION region{};
        if (*state.Slot != original || query() != host ||
            !VirtualQuery(published, &region, sizeof(region)) || region.State != MEM_COMMIT) return 5;
        printf("arch=%zu repeat=%d source unchanged, private mode, metrics refresh and restoration passed; bytes=%zu\n",
            sizeof(void*) * 8, repeat, state.Size);
    }
    return 0;
}
