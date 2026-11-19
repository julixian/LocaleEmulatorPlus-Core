#include "stdafx.h"
#include "ElevationLocator.h"

namespace lep
{
    namespace
    {
        struct ImageView
        {
            PBYTE Base;
            PIMAGE_NT_HEADERS Nt;
            ULONG Size;

            BOOL Contains(PVOID Address, SIZE_T Length) const
            {
                ULONG_PTR Offset = (ULONG_PTR)Address - (ULONG_PTR)Base;
                return Offset <= Size && Length <= Size - Offset;
            }

            BOOL Executable(PVOID Address, SIZE_T Length = 1) const
            {
                if (!Contains(Address, Length)) return FALSE;
                ULONG_PTR Offset = (PBYTE)Address - Base;
                auto Section = IMAGE_FIRST_SECTION(Nt);
                for (ULONG i = 0; i < Nt->FileHeader.NumberOfSections; ++i, ++Section)
                {
                    if ((Section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0 &&
                        Offset >= Section->VirtualAddress &&
                        Offset - Section->VirtualAddress <= Section->Misc.VirtualSize &&
                        Length <= Section->Misc.VirtualSize - (Offset - Section->VirtualAddress))
                        return TRUE;
                }
                return FALSE;
            }
        };

        BOOL HasDirectCaller(const ImageView& Image, PBYTE Routine)
        {
            auto Section = IMAGE_FIRST_SECTION(Image.Nt);
            for (ULONG i = 0; i < Image.Nt->FileHeader.NumberOfSections; ++i, ++Section)
            {
                if ((Section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 ||
                    Section->Misc.VirtualSize < 5 ||
                    !Image.Contains(Image.Base + Section->VirtualAddress, Section->Misc.VirtualSize)) continue;
                PBYTE Begin = Image.Base + Section->VirtualAddress;
                for (ULONG j = 0; j <= Section->Misc.VirtualSize - 5; ++j)
                    if (Begin[j] == 0xE8 && Begin + j + 5 + *(LONG*)(Begin + j + 1) == Routine)
                        return TRUE;
            }
            return FALSE;
        }

#if ML_AMD64
        PBYTE FunctionStart(const ImageView& Image, PBYTE Reference)
        {
            auto Directory = Image.Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
            if (Directory.Size < 12 || !Image.Contains(Image.Base + Directory.VirtualAddress, Directory.Size))
                return nullptr;
            struct Entry { ULONG Begin, End, Unwind; };
            auto Entries = (Entry*)(Image.Base + Directory.VirtualAddress);
            ULONG Offset = (ULONG)(Reference - Image.Base);
            ULONG Low = 0, High = Directory.Size / sizeof(Entry);
            while (Low < High)
            {
                ULONG Mid = Low + (High - Low) / 2;
                if (Offset < Entries[Mid].Begin) High = Mid;
                else if (Offset >= Entries[Mid].End) Low = Mid + 1;
                else
                {
                    PBYTE Begin = Image.Base + Entries[Mid].Begin;
                    // All verified versions save RBX, RSI, RDI, R12..R15 and
                    // allocate the large RPC/STARTUPINFO frame. The COM client
                    // also references this GUID but has a different prologue.
                    static const BYTE Prologue[] = {0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC};
                    if (!Image.Executable(Begin, 80) ||
                        !((Begin[0] == 0x40 && Begin[1] == 0x53) || (Begin[0] == 0xFF && Begin[1] == 0xF3)) ||
                        RtlCompareMemory(Begin + 2, Prologue, sizeof(Prologue)) != sizeof(Prologue)) return nullptr;
                    ULONG Frame = *(ULONG*)(Begin + 15);
                    if (Frame < 0x200 || Frame > 0x2000 || Reference - Begin > 0x400) return nullptr;
                    BOOL SavesR9 = FALSE;
                    for (ULONG j = 19; j < 72; ++j)
                        if (Begin[j] == 0x44 && Begin[j+1] == 0x89 && Begin[j+2] == 0x8C && Begin[j+3] == 0x24)
                            SavesR9 = TRUE;
                    return SavesR9 ? Begin : nullptr;
                }
            }
            return nullptr;
        }
#else
        ElevationEntry FunctionStart(const ImageView& Image, PBYTE Reference)
        {
            ElevationEntry Result{};
            ULONG Distance = (ULONG)(Reference - Image.Base);
            if (Distance > 0x400) Distance = 0x400;
            for (ULONG Back = 15; Back <= Distance; ++Back)
            {
                PBYTE Begin = Reference - Back;
                if (!Image.Executable(Begin, 80) || Begin[0] != 0x68 || Begin[5] != 0x68 || Begin[10] != 0xE8)
                    continue;
                ULONG Frame = *(ULONG*)(Begin + 1);
                if (Frame < 0x200 || Frame > 0x2000 ||
                    !Image.Contains((PVOID)(ULONG_PTR)*(ULONG*)(Begin+6), 16) ||
                    !Image.Executable(Begin + 15 + *(LONG*)(Begin+11))) continue;
                BOOL Registers = Begin[15] == 0x89 && Begin[16] == 0x8D && Begin[21] == 0x89 && Begin[22] == 0x95;
                BOOL Stack = Begin[15] == 0x8B && Begin[16] == 0x45 && Begin[17] == 8 &&
                    Begin[24] == 0x8B && Begin[25] == 0x45 && Begin[26] == 12;
                if (!Registers && !Stack) continue;
                // Verify the actual callee cleanup, rather than trusting the
                // original stdcall declaration retained in optimized PDBs.
                // Win7 uses call __SEH_epilog4 (E8 ...) before ret 0x24;
                // Win10/11 inline the epilog ending with leave (C9) before ret 0x1C.
                BOOL Cleanup = FALSE;
                for (ULONG j = 20; j < 0x800 && Image.Executable(Begin + j, 3); ++j)
                {
                    if (Begin[j] == 0xC2 &&
                        *(USHORT*)(Begin + j + 1) == (Registers ? 28 : 36) &&
                        (Begin[j - 5] == 0xE8 || Begin[j - 1] == 0xC9))
                    {
                        Cleanup = TRUE;
                        break;
                    }
                }
                if (!Cleanup) continue;
                if (Result.Routine != nullptr && Result.Routine != Begin) return ElevationEntry{};
                Result.Routine = Begin; Result.RegisterArguments = Registers;
            }
            return Result;
        }
#endif
    }

    ElevationEntry FindElevationEntry(PVOID Module)
    {
        ElevationEntry Result{};
        if (Module == nullptr || ((PIMAGE_DOS_HEADER)Module)->e_magic != IMAGE_DOS_SIGNATURE) return Result;
        auto Dos = (PIMAGE_DOS_HEADER)Module;
        if (Dos->e_lfanew < sizeof(*Dos) || Dos->e_lfanew > 0x1000) return Result;
        auto Nt = (PIMAGE_NT_HEADERS)((PBYTE)Module + Dos->e_lfanew);
        if (Nt->Signature != IMAGE_NT_SIGNATURE || Nt->FileHeader.NumberOfSections > 96 ||
            Nt->OptionalHeader.SizeOfImage > 0x10000000 || Nt->OptionalHeader.SizeOfImage < 0x1000) return Result;
#if ML_AMD64
        if (Nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return Result;
#else
        if (Nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return Result;
#endif
        ImageView Image{(PBYTE)Module, Nt, Nt->OptionalHeader.SizeOfImage};
        if (!Image.Contains(IMAGE_FIRST_SECTION(Nt), Nt->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER))) return Result;
        static const WCHAR InterfaceId[] = L"201ef99a-7fa0-444c-9399-19ba84f12a1a";
        PBYTE Identifier = nullptr;
        auto Section = IMAGE_FIRST_SECTION(Nt);
        for (ULONG i = 0; i < Nt->FileHeader.NumberOfSections; ++i, ++Section)
        {
            if ((Section->Characteristics & IMAGE_SCN_MEM_READ) == 0 || Section->Misc.VirtualSize < sizeof(InterfaceId) ||
                !Image.Contains(Image.Base + Section->VirtualAddress, Section->Misc.VirtualSize)) continue;
            PBYTE Begin = Image.Base + Section->VirtualAddress;
            for (ULONG j = 0; j <= Section->Misc.VirtualSize - sizeof(InterfaceId); j += 2)
                if (RtlCompareMemory(Begin+j, InterfaceId, sizeof(InterfaceId)) == sizeof(InterfaceId))
                {
                    if (Identifier != nullptr) return Result;
                    Identifier = Begin+j;
                }
        }
        if (Identifier == nullptr) return Result;
        Section = IMAGE_FIRST_SECTION(Nt);
        for (ULONG i = 0; i < Nt->FileHeader.NumberOfSections; ++i, ++Section)
        {
            if ((Section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 || Section->Misc.VirtualSize < 7 ||
                !Image.Contains(Image.Base + Section->VirtualAddress, Section->Misc.VirtualSize)) continue;
            PBYTE Begin = Image.Base + Section->VirtualAddress;
            for (ULONG j = 0; j <= Section->Misc.VirtualSize - 7; ++j)
            {
                PBYTE Reference = Begin+j;
                ElevationEntry Candidate{};
#if ML_AMD64
                if (Reference[0] != 0x48 || Reference[1] != 0x8D || Reference[2] != 0x0D ||
                    Reference+7+*(LONG*)(Reference+3) != Identifier) continue;
                Candidate.Routine = FunctionStart(Image, Reference);
#else
                if ((Reference[0] != 0x68 && Reference[0] != 0xB9) ||
                    *(ULONG*)(Reference+1) != (ULONG_PTR)Identifier) continue;
                Candidate = FunctionStart(Image, Reference);
#endif
                if (Candidate.Routine == nullptr || !HasDirectCaller(Image, (PBYTE)Candidate.Routine)) continue;
                if (Result.Routine != nullptr && Result.Routine != Candidate.Routine) return ElevationEntry{};
                Result = Candidate;
            }
        }
        return Result;
    }
}
