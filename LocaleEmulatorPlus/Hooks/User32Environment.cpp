#include "stdafx.h"

namespace lep
{
    static BOOL ReadableRegion(PVOID Address, SIZE_T Needed, MEMORY_BASIC_INFORMATION* Region)
    {
        if (!NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(), Address, MemoryBasicInformation,
                Region, sizeof(*Region), nullptr)) || Region->State != MEM_COMMIT ||
            (Region->Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
            return FALSE;
        SIZE_T Offset = (PBYTE)Address - (PBYTE)Region->BaseAddress;
        return Offset <= Region->RegionSize && Needed <= Region->RegionSize - Offset;
    }

    static PBYTE* FindServerInfoSlot(PVOID Module)
    {
        auto Code = (PBYTE)Nt_GetProcAddress(Module, "CheckDBCSEnabledExt");
        if (Code == nullptr)
            return nullptr;
        auto Nt = (PIMAGE_NT_HEADERS)PtrAdd(Module, ((PIMAGE_DOS_HEADER)Module)->e_lfanew);
        PBYTE End = (PBYTE)Module + Nt->OptionalHeader.SizeOfImage;
        if (Code < (PBYTE)Module || Code > End - 32)
            return nullptr;

        // Decode the public query's actual operation: load *gpsi, read its
        // first DWORD, shift bit 1 down and mask it to a BOOL. No private RVA
        // or SERVERINFO member layout is assumed beyond this verified access.
        static const BYTE Query[] = {0x8B, 0x00, 0xD1, 0xE8, 0x83, 0xE0, 0x01, 0xC3};
        PBYTE* Found = nullptr;
        for (ULONG i = 0; i != 16; ++i)
        {
            PBYTE* Candidate = nullptr;
#if ML_AMD64
            if (Code[i] == 0x48 && Code[i + 1] == 0x8B && Code[i + 2] == 0x05 &&
                RtlCompareMemory(Code + i + 7, Query, sizeof(Query)) == sizeof(Query))
                Candidate = (PBYTE*)(Code + i + 7 + *(LONG*)(Code + i + 3));
#else
            if (Code[i] == 0xA1 && RtlCompareMemory(Code + i + 5, Query, sizeof(Query)) == sizeof(Query))
                Candidate = (PBYTE*)(ULONG_PTR)*(ULONG*)(Code + i + 1);
#endif
            if (Candidate == nullptr)
                continue;
            MEMORY_BASIC_INFORMATION Region;
            if ((PBYTE)Candidate < (PBYTE)Module || (PBYTE)Candidate > End - sizeof(PVOID) ||
                ((ULONG_PTR)Candidate & (sizeof(PVOID) - 1)) != 0 ||
                !ReadableRegion(Candidate, sizeof(PVOID), &Region))
                return nullptr;
            if (Found != nullptr && Found != Candidate)
                return nullptr;
            Found = Candidate;
        }
        return Found;
    }

    static BOOL SetServerInfoSlot(PBYTE* Slot, PBYTE Value)
    {
        auto Patch = Mp::MemoryPatchVa((ULONG64)(ULONG_PTR)Value, sizeof(Value), Slot);
        return NT_SUCCESS(Mp::PatchMemory(&Patch, 1));
    }

    BOOL User32Environment::Initialize(PVOID Module, BOOL TargetDbcs)
    {
        if (LockReady)
            return FALSE;
        Source = Shadow = nullptr;
        Size = 0;
        Stopped = FALSE;
        Dbcs = TargetDbcs;
        Slot = FindServerInfoSlot(Module);
        if (Slot == nullptr || !NT_SUCCESS(RtlInitializeCriticalSectionAndSpinCount(&Lock, 4000)))
            return FALSE;
        LockReady = TRUE;
        return TRUE;
    }

    VOID User32Environment::Synchronize()
    {
        if (!LockReady)
            return;
        RtlEnterCriticalSection(&Lock);
        do
        {
            if (Stopped || Slot == nullptr || *Slot == nullptr)
                break;
            PBYTE Current = *Slot;
            PBYTE Original = Current == Shadow ? Source : Current;
            SIZE_T Available = Size;
            if (Original != Source || Available == 0)
            {
                MEMORY_BASIC_INFORMATION Region;
                if (!ReadableRegion(Original, sizeof(ULONG), &Region) || Region.Type != MEM_MAPPED)
                    break;
                Available = (PBYTE)Region.BaseAddress + Region.RegionSize - Original;
            }
            // Copy the verified readable mapping tail, rather than baking in
            // a version-specific SERVERINFO size. Unexpected sizes fail closed.
            if (Available < MEMORY_PAGE_SIZE || Available > 1024 * 1024)
                break;
            ULONG Flags = *(volatile ULONG*)Original;
            if (Shadow == nullptr && ((Flags & 2) != 0) == (Dbcs != FALSE))
            {
                Source = Original;
                Size = Available;
                break;
            }
            if (Original != Source || Available != Size || Shadow == nullptr)
            {
                auto NewShadow = (PBYTE)AllocateVirtualMemory(Available);
                if (NewShadow == nullptr)
                    break;
                CopyMemory(NewShadow, Original, Available);
                *(ULONG*)NewShadow = (Flags & ~2u) | (Dbcs ? 2u : 0u);
                if (!SetServerInfoSlot(Slot, NewShadow) && *Slot != NewShadow)
                {
                    ::FreeVirtualMemory(NewShadow);
                    break;
                }
                // A USER32 reader on another thread may already hold the old
                // pointer. Retired published copies stay mapped until process
                // exit; unpublished allocation failures are freed immediately.
                Source = Original;
                Shadow = NewShadow;
                Size = Available;
                WriteLog(L"user32 private environment bytes=%u dbcs=%u", (ULONG)Size, Dbcs);
            }
            else
            {
                // Keep live metrics/settings from the original server mapping.
                // Never copy its host DBCS bit over the process-local override.
                CopyMemory(Shadow + sizeof(ULONG), Source + sizeof(ULONG), Size - sizeof(ULONG));
                InterlockedExchange((LONG*)Shadow, (LONG)((Flags & ~2u) | (Dbcs ? 2u : 0u)));
            }
        } while (FALSE);
        RtlLeaveCriticalSection(&Lock);
    }

    VOID User32Environment::Restore()
    {
        if (!LockReady)
            return;
        RtlEnterCriticalSection(&Lock);
        Stopped = TRUE;
        MEMORY_BASIC_INFORMATION Region;
        if (Shadow != nullptr && Slot != nullptr && ReadableRegion(Slot, sizeof(PVOID), &Region) && *Slot == Shadow)
            SetServerInfoSlot(Slot, Source);
        // Published copies intentionally remain valid for concurrent native
        // readers. Windows reclaims these process-private mappings on exit.
        RtlLeaveCriticalSection(&Lock);
        // Restore is called only after removing our hooks, with callers
        // quiescent, just like the surrounding USER32 unhook operation.
        LockReady = FALSE;
        RtlDeleteCriticalSection(&Lock);
    }
}
