#include "../LocaleEmulatorPlus/stdafx.h"
#include "../LocaleEmulatorPlus/Hooks/ElevationLocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static bool VerifySample(PCWSTR Label, PCWSTR Path, ULONG ExpectedRva, int ExpectedRegArgs)
{
    PVOID Image = nullptr;
    NTSTATUS Status = LoadPeImage(Path, &Image, nullptr, LOAD_PE_IGNORE_IAT);
    if (NT_FAILED(Status) || Image == nullptr)
    {
        wprintf(L"[FAIL] %s: LoadPeImage(%s) failed status=%08X\n", Label, Path, (ULONG)Status);
        return false;
    }

    lep::ElevationEntry Entry = lep::FindElevationEntry(Image);
    ULONG ActualRva = Entry.Routine != nullptr
        ? (ULONG)((PBYTE)Entry.Routine - (PBYTE)Image)
        : 0;
    BOOL ActualReg = Entry.RegisterArguments != FALSE;
    UnloadPeImage(Image);

    bool Ok = false;
    if (ExpectedRva == 0)
    {
        Ok = (Entry.Routine == nullptr);
    }
    else
    {
        Ok = (ActualRva == ExpectedRva) && (ExpectedRegArgs < 0 || ActualReg == (ExpectedRegArgs != 0));
    }

    wprintf(L"[%s] %-16s rva=0x%X (expect=0x%X) regArgs=%u (expect=%d) path=%s\n",
            Ok ? L"PASS" : L"FAIL", Label, ActualRva, ExpectedRva,
            (ULONG)ActualReg, ExpectedRegArgs, Path);
    return Ok;
}

int __cdecl wmain(int argc, wchar_t** argv)
{
    lep::InitializeSupportHeap();
    bool AllPassed = true;

    // Live host check: Windows.Storage.dll must match; host shell32.dll on Win10/11 must not.
    HMODULE Storage = LoadLibraryW(L"Windows.Storage.dll");
    bool HasStorage = (Storage != nullptr);
    if (Storage != nullptr)
    {
        lep::ElevationEntry Entry = lep::FindElevationEntry(Storage);
        ULONG Rva = Entry.Routine != nullptr ? (ULONG)((PBYTE)Entry.Routine - (PBYTE)Storage) : 0;
        bool Ok = Entry.Routine != nullptr;
#if !ML_AMD64
        Ok = Ok && (Entry.RegisterArguments != FALSE);
#endif
        wprintf(L"[%s] %-16s rva=0x%X regArgs=%u\n",
                Ok ? L"PASS" : L"FAIL", L"live-storage", Rva, (ULONG)(Entry.RegisterArguments != FALSE));
        AllPassed = AllPassed && Ok;
        FreeLibrary(Storage);
    }

    HMODULE Shell32 = LoadLibraryW(L"shell32.dll");
    if (Shell32 != nullptr)
    {
        lep::ElevationEntry Entry = lep::FindElevationEntry(Shell32);
        ULONG Rva = Entry.Routine != nullptr ? (ULONG)((PBYTE)Entry.Routine - (PBYTE)Shell32) : 0;
        // On Win10/11, shell32 forwards to Windows.Storage and must not match.
        bool Ok = HasStorage ? (Entry.Routine == nullptr) : (Entry.Routine != nullptr);
        wprintf(L"[%s] %-16s rva=0x%X regArgs=%u\n",
                Ok ? L"PASS" : L"FAIL", L"live-shell32", Rva, (ULONG)(Entry.RegisterArguments != FALSE));
        AllPassed = AllPassed && Ok;
        FreeLibrary(Shell32);
    }

    // Optional offline sample tuples: <label> <path> <expectedHexRva> <expectedRegArgs(-1|0|1)>
    for (int i = 1; i + 3 < argc; i += 4)
    {
        ULONG ExpectedRva = wcstoul(argv[i + 2], nullptr, 16);
        int ExpectedReg = _wtoi(argv[i + 3]);
        AllPassed = VerifySample(argv[i], argv[i + 1], ExpectedRva, ExpectedReg) && AllPassed;
    }

    lep::UninitializeSupportHeap();
    return AllPassed ? 0 : 1;
}
