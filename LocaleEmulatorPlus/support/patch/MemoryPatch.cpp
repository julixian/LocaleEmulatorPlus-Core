#include "../Support.h"
#if CPP_DEFINED
#pragma push_macro("POINTER_SIZE")
#undef POINTER_SIZE
#define POINTER_SIZE sizeof(PVOID)
ML_NAMESPACE_BEGIN(Mp);

#if ML_X86

#define SIZE_OF_JUMP_CODE   5

#elif ML_AMD64


#endif // arch

VOID PatchNop(PVOID Address, ULONG_PTR BytesToPatch)
{
    PBYTE Buffer = (PBYTE)Address;

    while (BytesToPatch != 0)
    {
        *Buffer++ = 0x90;
        --BytesToPatch;
    }
}

class MemoryPatchManager
{
protected:
    lep::DynamicArray<PTRAMPOLINE_DATA> TrampolineList;
    HANDLE ExecutableHeap;

public:
    MemoryPatchManager()
    {
        this->ExecutableHeap = RtlCreateHeap(HEAP_CREATE_ENABLE_EXECUTE | HEAP_GROWABLE | HEAP_CREATE_ALIGN_16, nullptr, 0, 0, nullptr, nullptr);
    }

protected:
    NoInline NTSTATUS HandleMemoryPatch(PPATCH_MEMORY_DATA PatchData, PVOID BaseAddress)
    {
        auto&       Patch = PatchData->Memory;
        PVOID       Address;
        ULONG       Protect;
        NTSTATUS    Status;

        if (Patch.Size == 0)
            return STATUS_SUCCESS;

        if (Patch.Address == IMAGE_INVALID_RVA)
            return STATUS_SUCCESS;

        Address = PtrAdd(Patch.Options.VirtualAddress ? nullptr : BaseAddress, Patch.Address);
        Status = ProtectMemory(Address, Patch.Size, PAGE_EXECUTE_READWRITE, &Protect);
        FAIL_RETURN(Status);

        if (Patch.Options.DataIsBuffer)
        {
            CopyMemory(Address, (PVOID)Patch.Data, Patch.Size);
        }
        else
        {
            if (Patch.Options.BackupData)
            {
                PTRAMPOLINE_DATA TrampolineData;

                TrampolineData = AllocateTrampolineData();
                if (TrampolineData == nullptr)
                    return STATUS_NO_MEMORY;

                TrampolineData->PatchData = *PatchData;
                TrampolineData->PatchData.Memory.Backup = 0;
                TrampolineData->PatchData.Memory.Options.VirtualAddress = TRUE;
                TrampolineData->PatchData.Memory.Address = (ULONG_PTR)Address;
                CopyMemory(&TrampolineData->PatchData.Memory.Backup, Address, Patch.Size);

                *PatchData = TrampolineData->PatchData;

                this->TrampolineList.Add(TrampolineData);
            }

            CopyMemory(&Patch.Backup, Address, Patch.Size);
            CopyMemory(Address, &Patch.Data, Patch.Size);
        }

        Status = ProtectMemory(Address, Patch.Size, Protect, nullptr);

        return STATUS_SUCCESS;
    }

#if ML_AMD64
    BOOL GetRelativeJumpOffset(PVOID Address, PVOID Target, PLONG RelativeOffset)
    {
        LONGLONG Offset;

        Offset = (LONGLONG)(ULONG_PTR)Target -
                 (LONGLONG)((ULONG_PTR)Address + GetSizeOfHookOpSize(OpJump));
        if (Offset < -0x80000000LL || Offset > 0x7FFFFFFFLL)
            return FALSE;

        *RelativeOffset = (LONG)Offset;
        return TRUE;
    }

    ULONG_PTR AlignDownTo(ULONG_PTR Value, ULONG_PTR Alignment)
    {
        return Value & ~(Alignment - 1);
    }

    ULONG_PTR AlignUpTo(ULONG_PTR Value, ULONG_PTR Alignment)
    {
        return AlignDownTo(Value + Alignment - 1, Alignment);
    }

    NTSTATUS TryAllocateRelayStub(PVOID EntryAddress, PVOID CandidateBase, PVOID Target, PVOID* Relay)
    {
        PVOID Base;
        SIZE_T Size;
        NTSTATUS Status;
        LONG RelativeOffset;

        Base = CandidateBase;
        Size = TRAMPOLINE_SIZE;
        Status = NtAllocateVirtualMemory(CurrentProcess, &Base, 0, &Size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (NT_FAILED(Status))
            return Status;

        if (!GetRelativeJumpOffset(EntryAddress, Base, &RelativeOffset))
        {
            Mm::FreeVirtualMemory(Base);
            return STATUS_BUFFER_TOO_SMALL;
        }

        Status = GenerateHookCode((PBYTE)Base, Base, Target, OpJumpIndirect, GetSizeOfHookOpSize(OpJumpIndirect));
        if (NT_FAILED(Status))
        {
            Mm::FreeVirtualMemory(Base);
            return Status;
        }

        FlushInstructionCache(Base, GetSizeOfHookOpSize(OpJumpIndirect));
        *Relay = Base;

        return STATUS_SUCCESS;
    }

    NTSTATUS AllocateRelayStub(PVOID Address, PVOID Target, PVOID* Relay)
    {
        ULONG_PTR Near, Low, High, Query, Next;
        MEMORY_BASIC_INFORMATION MemoryInfo;
        NTSTATUS Status;

        static const ULONG_PTR AllocationGranularity = 0x10000;
        static const ULONG_PTR MinUserAddress = 0x10000;
        static const ULONG_PTR MaxRange = 0x7FFFFFFF;

        *Relay = nullptr;

        Near = (ULONG_PTR)Address;
        Low = Near > MaxRange ? Near - MaxRange : MinUserAddress;
        if (Low < MinUserAddress)
            Low = MinUserAddress;

        High = Near + MaxRange;
        if (High < Near)
            High = MAXULONG_PTR;

        Query = AlignDownTo(Near, AllocationGranularity);
        while (Query > Low)
        {
            Status = NtQueryVirtualMemory(CurrentProcess, (PVOID)Query, MemoryBasicInformation, &MemoryInfo, sizeof(MemoryInfo), nullptr);
            if (NT_FAILED(Status))
            {
                Query -= Query >= AllocationGranularity ? AllocationGranularity : Query;
                continue;
            }

            if (MemoryInfo.State == MEM_FREE)
            {
                Status = TryAllocateRelayStub(Address, (PVOID)Query, Target, Relay);
                if (NT_SUCCESS(Status))
                    return Status;
            }

            if ((ULONG_PTR)MemoryInfo.BaseAddress <= Low)
                break;

            Query = AlignDownTo((ULONG_PTR)MemoryInfo.BaseAddress - 1, AllocationGranularity);
        }

        Query = AlignUpTo(Near, AllocationGranularity);
        while (Query < High)
        {
            Status = NtQueryVirtualMemory(CurrentProcess, (PVOID)Query, MemoryBasicInformation, &MemoryInfo, sizeof(MemoryInfo), nullptr);
            if (NT_FAILED(Status))
            {
                Query += AllocationGranularity;
                continue;
            }

            if (MemoryInfo.State == MEM_FREE)
            {
                Status = TryAllocateRelayStub(Address, (PVOID)Query, Target, Relay);
                if (NT_SUCCESS(Status))
                    return Status;
            }

            Next = (ULONG_PTR)MemoryInfo.BaseAddress + MemoryInfo.RegionSize;
            if (Next <= Query)
                Query += AllocationGranularity;
            else
                Query = AlignUpTo(Next, AllocationGranularity);
        }

        return STATUS_BUFFER_TOO_SMALL;
    }

#endif

    VOID FreeRelayStub(PVOID Relay)
    {
#if ML_AMD64
        if (Relay != nullptr)
            Mm::FreeVirtualMemory(Relay);
#else
        UNREFERENCED_PARAMETER(Relay);
#endif
    }

    NoInline NTSTATUS HandleFunctionPatch(PPATCH_MEMORY_DATA PatchData, PVOID BaseAddress)
    {
        auto&               Function = PatchData->Function;
        BYTE                LocalHookBuffer[TRAMPOLINE_SIZE];
        PVOID               Address;
        PVOID               HookTarget;
        PVOID               Relay;
        PBYTE               Trampoline;
        ULONG               Protect;
        ULONG_PTR           HookOp, HookOpSize, CopyOpSize;
        ULONG               HookOp32;
        BOOL                ForceTrampolineData;
        NTSTATUS            Status;
        PTRAMPOLINE_DATA    TrampolineData;

        if (Function.Target == nullptr)
            return STATUS_SUCCESS;

        if (Function.Source == IMAGE_INVALID_RVA)
            return STATUS_SUCCESS;

        ULONG FunctionOptions = Function.Options.Flags;
        BOOL VirtualAddressOpt = FLAG_ON(FunctionOptions, VirtualAddress);
        BOOL NakedTrampolineOpt = FLAG_ON(FunctionOptions, NakedTrampoline);
        BOOL DoNotDisassembleOpt = FLAG_ON(FunctionOptions, DoNotDisassemble);
        BOOL KeepRawTrampolineOpt = FLAG_ON(FunctionOptions, KeepRawTrampoline);
        BOOL NoAbsoluteJumpOpt = FLAG_ON(FunctionOptions, NoAbsoluteJump);

        Address = PtrAdd(VirtualAddressOpt ? nullptr : BaseAddress, Function.Source);

        HookOp = (ULONG)Function.HookOp;
        HookOp32 = (ULONG)HookOp;
        HookTarget = Function.Target;
        Relay = nullptr;
        ForceTrampolineData = FALSE;

#if ML_AMD64
        if (HookOp32 == (ULONG)OpJumpIndirect && NakedTrampolineOpt == FALSE)
        {
            PVOID       NearAddress;
            ULONG_PTR   NearCopyOpSize, NearHookOpSize;
            LONG        Offset;

            NearHookOpSize = GetSizeOfHookOpSize(OpJump);
            NearAddress = Address;

            Status = GetHookAddressAndSize(Address, NearHookOpSize, &NearAddress, &NearCopyOpSize);
            if (NT_SUCCESS(Status))
            {
                if (GetRelativeJumpOffset(NearAddress, HookTarget, &Offset))
                {
                    HookOp = OpJump;
                    HookOpSize = NearHookOpSize;
                    Address = NearAddress;
                    CopyOpSize = NearCopyOpSize;
                    goto GOT_HOOK_ADDRESS_AND_SIZE;
                }

                Status = AllocateRelayStub(NearAddress, HookTarget, &Relay);
                if (NT_SUCCESS(Status))
                {
                    HookOp = OpJump;
                    HookOpSize = NearHookOpSize;
                    Address = NearAddress;
                    CopyOpSize = NearCopyOpSize;
                    HookTarget = Relay;
                    ForceTrampolineData = TRUE;
                    goto GOT_HOOK_ADDRESS_AND_SIZE;
                }
            }

            if (NoAbsoluteJumpOpt)
            {
                if (Relay != nullptr)
                    FreeRelayStub(Relay);

                return NT_SUCCESS(Status) ? STATUS_BUFFER_TOO_SMALL : Status;
            }
        }
#endif

        HookOpSize = GetSizeOfHookOpSize(HookOp);
        if (HookOpSize == ULONG_PTR_MAX)
            return STATUS_BUFFER_TOO_SMALL;

        Status = GetHookAddressAndSize(Address, HookOpSize, &Address, &CopyOpSize);
        FAIL_RETURN(Status);

GOT_HOOK_ADDRESS_AND_SIZE:

        if (CopyOpSize > TRAMPOLINE_SIZE)
            RaiseDebugBreak();

        FillMemory(LocalHookBuffer, CopyOpSize, 0x90);

        if (NakedTrampolineOpt == FALSE)
        {
            Status = GenerateHookCode(LocalHookBuffer, Address, HookTarget, HookOp, HookOpSize);
            if (NT_FAILED(Status) && Relay != nullptr)
                FreeRelayStub(Relay);
            FAIL_RETURN(Status);

            PatchNop(&LocalHookBuffer[HookOpSize], CopyOpSize - HookOpSize);
        }

        TrampolineData = nullptr;
        if (NakedTrampolineOpt != FALSE ||
            ForceTrampolineData ||
            (Function.Trampoline != nullptr && DoNotDisassembleOpt == FALSE))
        {
            TrampolineData = AllocateTrampolineData();
            if (TrampolineData == nullptr)
            {
                if (Relay != nullptr)
                    FreeRelayStub(Relay);

                return STATUS_NO_MEMORY;
            }

            TrampolineData->PatchData = *PatchData;
            TrampolineData->PatchData.Function.HookOp = HookOp;
            TrampolineData->PatchData.Function.NopBytes = CopyOpSize - HookOpSize;
            TrampolineData->PatchData.Function.Source = (ULONG_PTR)Address;
            TrampolineData->PatchData.Function.Options.Flags |= VirtualAddress;
            TrampolineData->Relay = Relay;
            Relay = nullptr;

            TrampolineData->OriginSize = CopyOpSize;
            CopyMemory(TrampolineData->OriginalCode, Address, CopyOpSize);

            TrampolineData->JumpBackAddress = PtrAdd(Address, CopyOpSize);
            Trampoline = TrampolineData->Trampoline;

            if (NakedTrampolineOpt == FALSE)
            {
                CopyTrampolineStub(Trampoline, Address, CopyOpSize);
                if (CopyOpSize == HookOpSize && HookOp == OpCall && KeepRawTrampolineOpt == FALSE)
                {
                    TrampolineData->Trampoline[0] = 0xE9;
                }
                else
                {
                    GenerateJumpBack(Trampoline, &TrampolineData->JumpBackAddress);
                }
            }
            else
            {
                GenerateNakedTrampoline(Trampoline, Address, CopyOpSize, TrampolineData);

                Status = GenerateHookCode(LocalHookBuffer, Address, TrampolineData->Trampoline, HookOp, HookOpSize);
                if (NT_FAILED(Status))
                {
                    FreeTrampolineData(TrampolineData);
                    return Status;
                }

                PatchNop(&LocalHookBuffer[HookOpSize], CopyOpSize - HookOpSize);
            }

            if (Trampoline - TrampolineData->Trampoline > TRAMPOLINE_SIZE)
                RaiseDebugBreak();

            FlushInstructionCache(TrampolineData->Trampoline, TRAMPOLINE_SIZE);

            if (Function.Trampoline != nullptr)
                *Function.Trampoline = TrampolineData->Trampoline;

            this->TrampolineList.Add(TrampolineData);
        }

        Status = ProtectMemory(Address, CopyOpSize, PAGE_EXECUTE_READWRITE, &Protect);
        if (NT_FAILED(Status))
        {
            FreeTrampolineData(TrampolineData);
            if (Relay != nullptr)
                FreeRelayStub(Relay);

            return Status;
        }

        CopyMemory(Address, LocalHookBuffer, CopyOpSize);
        FlushInstructionCache(Address, CopyOpSize);

        Status = ProtectMemory(Address, CopyOpSize, Protect, &Protect);

        return STATUS_SUCCESS;
    }

    NoInline NTSTATUS GenerateHookCode(PBYTE Buffer, PVOID SourceIp, PVOID Target, ULONG_PTR HookOp, ULONG_PTR HookOpSize)
    {
        ULONG_PTR RegisterIndex;
        ULONG HookOp32 = (ULONG)HookOp;

#if ML_X86

        switch (HookOp)
        {
            case OpCall:
            case OpJump:
                //
                // jmp imm
                //
                *Buffer++ = HookOp == OpCall ? 0xE8 : 0xE9;
                *(PULONG)Buffer = PtrOffset(Target, PtrAdd(SourceIp, HookOpSize));
                break;

            case OpPush:
                //
                // push imm
                // ret
                //
                *Buffer++ = 0x68;
                *(PULONG)Buffer = (ULONG)Target;
                Buffer += POINTER_SIZE;
                *Buffer = 0xC3;
                break;

            default:
                if (HookOp < OpJRax || HookOp > OpJRdi)
                    return STATUS_INVALID_PARAMETER;

                //
                // mov r32, imm
                // jmp r32
                //
                RegisterIndex = HookOp - OpJRax;
                *Buffer++ = 0xB8 + (BYTE)RegisterIndex;
                *(PVOID *)Buffer = Target;
                Buffer += POINTER_SIZE;
                *(PUSHORT)Buffer = (USHORT)(0xE0FF + RegisterIndex);
                break;
        }

#elif ML_AMD64

        switch (HookOp32)
        {
            case OpCall:
            case OpJump:
            {
                LONG_PTR Offset;

                Offset = (LONG_PTR)PtrOffset(Target, PtrAdd(SourceIp, HookOpSize));
                if ((LONG_PTR)(LONG)Offset != Offset)
                    return STATUS_BUFFER_TOO_SMALL;

                *Buffer++ = HookOp32 == OpCall ? 0xE8 : 0xE9;
                *(PLONG)Buffer = (LONG)Offset;
                break;
            }

            case OpJumpIndirect:
                //
                // jmp qword ptr [rip + 0]
                // dq target
                //
                *Buffer++ = 0xFF;
                *Buffer++ = 0x25;
                *(PULONG)Buffer = 0;
                Buffer += sizeof(ULONG);
                *(PVOID *)Buffer = Target;
                break;

            case OpPush:
                //
                // push imm.low
                // mov dword ptr [rsp + 4], imm.high
                // ret
                //
                *Buffer++ = 0x68;
                *(PULONG)Buffer = (ULONG)((ULONG_PTR)Target >> 32);
                Buffer += POINTER_SIZE;

                *Buffer++ = 0xC7;
                *Buffer++ = 0x04;
                *Buffer++ = 0x24;
                *(PULONG)Buffer = (ULONG)(ULONG_PTR)Target;

                *Buffer = 0xC3;
                break;

            default:
                if (HookOp32 >= OpJRax && HookOp32 <= OpJRdi)
                {
                    //
                    // mov r64, imm
                    // jmp r64
                    //
                    RegisterIndex = HookOp32 - OpJRax;
                    *Buffer++ = 0x48;
                    *Buffer++ = 0xB8 + (BYTE)RegisterIndex;
                    *(PVOID *)Buffer = Target;
                    Buffer += POINTER_SIZE;
                    *(PUSHORT)Buffer = (USHORT)(0xE0FF + RegisterIndex);
                    break;
                }

                if (HookOp32 == OpJR10 || HookOp32 == OpJR11)
                {
                    //
                    // mov r10/r11, imm
                    // jmp r10/r11
                    //
                    RegisterIndex = HookOp32 == OpJR10 ? 2 : 3;
                    *Buffer++ = 0x49;
                    *Buffer++ = 0xBA + (BYTE)(RegisterIndex - 2);
                    *(PVOID *)Buffer = Target;
                    Buffer += POINTER_SIZE;
                    *Buffer++ = 0x41;
                    *(PUSHORT)Buffer = (USHORT)(0xE2FF + (RegisterIndex - 2));
                    break;
                }

                return STATUS_INVALID_PARAMETER;
                break;
        }

#else

        return STATUS_NOT_IMPLEMENTED;

#endif

        return STATUS_SUCCESS;
    }

    NoInline NTSTATUS GenerateNakedTrampoline(PBYTE& Trampoline, PVOID Address, ULONG_PTR CopyOpSize, PTRAMPOLINE_DATA TrampolineData)
    {
        NTSTATUS    Status;
        PVOID*      AddressOfReturnAddress;
        auto&       Function = TrampolineData->PatchData.Function;
        ULONG       FunctionOptions = Function.Options.Flags;
        BOOL        ExecuteTrampolineOpt = FLAG_ON(FunctionOptions, ExecuteTrampoline);

        if (ExecuteTrampolineOpt != FALSE)
        {
            Status = CopyTrampolineStub(Trampoline, Address, CopyOpSize);
            FAIL_RETURN(Status);
        }

#if ML_X86

        //
        // push     eax
        // push     esp
        // push     eax
        // push     ecx
        // push     edx
        // push     ebx
        // push     ebp
        // push     esi
        // push     edi
        // pushfd
        //
        // mov      eax, ReturnAddress
        // mov      [esp + 24h], eax
        // mov      eax, CallBack
        // lea      ecx, [esp];      ecx = PTRAMPOLINE_NAKED_CONTEXT
        // call     eax
        //
        // popfd
        // pop      edi
        // pop      esi
        // pop      ebp
        // pop      ebx
        // pop      edx
        // pop      ecx
        // pop      eax
        // pop      esp
        //
        // ret
        //

        if (Function.HookOp != OpCall)
        {
            // push eax
            *Trampoline++ = 0x50;
        }

        *Trampoline++ = 0x54;       // push    esp
        *Trampoline++ = 0x50;       // push    eax
        *Trampoline++ = 0x51;       // push    ecx
        *Trampoline++ = 0x52;       // push    edx
        *Trampoline++ = 0x53;       // push    ebx
        *Trampoline++ = 0x55;       // push    ebp
        *Trampoline++ = 0x56;       // push    esi
        *Trampoline++ = 0x57;       // push    edi
        *Trampoline++ = 0x9C;       // pushfd

        // mov eax, imm
        *Trampoline++ = 0xB8;
        *(PVOID *)Trampoline = PtrAdd(Address, CopyOpSize);
        AddressOfReturnAddress = (PVOID *)Trampoline;
        Trampoline += POINTER_SIZE;

        // mov [esp + 24h], eax
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x44;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x24;

        // mov eax, CallBack
        *Trampoline++ = 0xB8;
        *(PVOID *)Trampoline = Function.Target;
        Trampoline += POINTER_SIZE;

        // Lepa ecx, [esp]
        *Trampoline++ = 0x8D;
        *Trampoline++ = 0x0C;
        *Trampoline++ = 0x24;

        // call eax
        *Trampoline++ = 0xFF;
        *Trampoline++ = 0xD0;

        *Trampoline++ = 0x9D;       // popfd
        *Trampoline++ = 0x5F;       // pop     edi
        *Trampoline++ = 0x5E;       // pop     esi
        *Trampoline++ = 0x5D;       // pop     ebp
        *Trampoline++ = 0x5B;       // pop     ebx
        *Trampoline++ = 0x5A;       // pop     edx
        *Trampoline++ = 0x59;       // pop     ecx
        *Trampoline++ = 0x58;       // pop     eax
        *Trampoline++ = 0x5C;       // pop     esp

        // ret
        *Trampoline++ = 0xC3;

#else

        //
        // SIZE_OF_CONTEXT equ 78h
        // push    rax
        // pushfq
        // lea     rsp, [rsp - SIZE_OF_CONTEXT]
        // mov     [rsp + 00h], rax
        // mov     [rsp + 08h], rcx
        // mov     [rsp + 10h], rdx
        // mov     [rsp + 18h], rbx
        // mov     [rsp + 20h], rbp
        // mov     [rsp + 28h], rsi
        // mov     [rsp + 30h], rdi
        // mov     [rsp + 38h], r8
        // mov     [rsp + 40h], r9
        // mov     [rsp + 48h], r10
        // mov     [rsp + 50h], r11
        // mov     [rsp + 58h], r12
        // mov     [rsp + 60h], r13
        // mov     [rsp + 68h], r14
        // mov     [rsp + 70h], r15
        // mov     rax, ReturnAddress
        // mov     [rsp + SIZE_OF_CONTEXT + 10h], rax
        // mov     rax, CallBack
        // lea     rcx, [rsp];      rcx = PTRAMPOLINE_NAKED_CONTEXT
        // lea     rsp, [rsp - 20h]
        // call    rax
        // lea     rsp, [rsp + 20h]
        // mov     rax, [rsp + 00h]
        // mov     rcx, [rsp + 08h]
        // mov     rdx, [rsp + 10h]
        // mov     rbx, [rsp + 18h]
        // mov     rbp, [rsp + 20h]
        // mov     rsi, [rsp + 28h]
        // mov     rdi, [rsp + 30h]
        // mov     r8 , [rsp + 38h]
        // mov     r9 , [rsp + 40h]
        // mov     r10, [rsp + 48h]
        // mov     r11, [rsp + 50h]
        // mov     r12, [rsp + 58h]
        // mov     r13, [rsp + 60h]
        // mov     r14, [rsp + 68h]
        // mov     r15, [rsp + 70h]
        // lea     rsp, [rsp + SIZE_OF_CONTEXT]
        // popfq
        // ret
        //

        if (Function.HookOp != OpCall)
        {
            // push rax
            *Trampoline++ = 0x50;
        }

        // pushfq
        *Trampoline++ = 0x9C;

        // lea     rsp, [rsp-78h]
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x8D;
        *Trampoline++ = 0x64;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x88;

        // mov     [rsp + 00h], rax
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x04;
        *Trampoline++ = 0x24;

        // mov     [rsp + 08h], rcx
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x08;

        // mov     [rsp + 10h], rdx
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x54;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x10;

        // mov     [rsp + 18h], rbx
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x5C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x18;

        // mov     [rsp + 20h], rbp
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x6C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x20;

        // mov     [rsp + 28h], rsi
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x74;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x28;

        // mov     [rsp + 30h], rdi
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x7C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x30;

        // mov     [rsp + 38h], r8
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x44;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x38;

        // mov     [rsp + 40h], r9
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x40;

        // mov     [rsp + 48h], r10
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x54;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x48;

        // mov     [rsp + 50h], r11
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x5C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x50;

        // mov     [rsp + 58h], r12
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x64;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x58;

        // mov     [rsp + 60h], r13
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x6C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x60;

        // mov     [rsp + 68h], r14
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x74;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x68;

        // mov     [rsp + 70h], r15
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x7C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x70;

        // mov     rax, ReturnAddress
        *Trampoline++ = 0x48;
        *Trampoline++ = 0xB8;
        *(PVOID *)Trampoline = PtrAdd(Address, CopyOpSize);
        AddressOfReturnAddress = (PVOID *)Trampoline;
        Trampoline += POINTER_SIZE;

        // mov     [rsp + SIZE_OF_CONTEXT + 10], rax
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x89;
        *Trampoline++ = 0x84;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x80;
        *Trampoline++ = 0x00;
        *Trampoline++ = 0x00;
        *Trampoline++ = 0x00;

        // mov     rax, CallBack
        *Trampoline++ = 0x48;
        *Trampoline++ = 0xB8;
        *(PVOID *)Trampoline = Function.Target;
        Trampoline += POINTER_SIZE;

        // lea     rcx, [rsp];      rcx = PTRAMPOLINE_NAKED_CONTEXT
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x8D;
        *Trampoline++ = 0x0C;
        *Trampoline++ = 0x24;

        // lea     rsp, [rsp - 20h]
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x8D;
        *Trampoline++ = 0x64;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0xE0;

        // call    rax
        *Trampoline++ = 0xFF;
        *Trampoline++ = 0xD0;

        // lea     rsp, [rsp + 20h]
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x8D;
        *Trampoline++ = 0x64;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x20;

        // mov     rax, [rsp + 00h]
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x04;
        *Trampoline++ = 0x24;

        // mov     rcx, [rsp + 08h]
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x08;

        // mov     rdx, [rsp + 10h]
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x54;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x10;

        // mov     rbx, [rsp + 18h]
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x5C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x18;

        // mov     rbp, [rsp + 20h]
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x6C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x20;

        // mov     rsi, [rsp + 28h]
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x74;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x28;

        // mov     rdi, [rsp + 30h]
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x7C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x30;

        // mov     r8 , [rsp + 38h]
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x44;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x38;

        // mov     r9 , [rsp + 40h]
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x40;

        // mov     r10, [rsp + 48h]
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x54;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x48;

        // mov     r11, [rsp + 50h]
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x5C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x50;

        // mov     r12, [rsp + 58h]
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x64;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x58;

        // mov     r13, [rsp + 60h]
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x6C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x60;

        // mov     r14, [rsp + 68h]
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x74;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x68;

        // mov     r15, [rsp + 70h]
        *Trampoline++ = 0x4C;
        *Trampoline++ = 0x8B;
        *Trampoline++ = 0x7C;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x70;

        // lea     rsp, [rsp + SIZE_OF_CONTEXT]
        *Trampoline++ = 0x48;
        *Trampoline++ = 0x8D;
        *Trampoline++ = 0x64;
        *Trampoline++ = 0x24;
        *Trampoline++ = 0x78;

        // popfq
        *Trampoline++ = 0x9D;

        // ret
        *Trampoline++ = 0xC3;

//#error NOT COMPLETE

#endif // arch

        if (ExecuteTrampolineOpt == FALSE)
        {
            *AddressOfReturnAddress = Trampoline;

            Status = CopyTrampolineStub(Trampoline, Address, CopyOpSize);
            FAIL_RETURN(Status);

            Status = GenerateJumpBack(Trampoline, &TrampolineData->JumpBackAddress);
            FAIL_RETURN(Status);
        }

        return STATUS_SUCCESS;
    }

    NoInline NTSTATUS GenerateJumpBack(PBYTE& Trampoline, PVOID AddressOfJumpAddress)
    {
#if ML_AMD64
        // jmp qword ptr [rip + 0]
        // dq *AddressOfJumpAddress
        *Trampoline++ = 0xFF;
        *Trampoline++ = 0x25;
        *(PULONG)Trampoline = 0;
        Trampoline += sizeof(ULONG);
        *(PVOID *)Trampoline = *(PVOID *)AddressOfJumpAddress;
        Trampoline += sizeof(PVOID);
#else
        // jmp [imm32]
        *Trampoline++ = 0xFF;
        *Trampoline++ = 0x25;

        *(PVOID *)Trampoline = AddressOfJumpAddress;
        Trampoline += 2 + 4;
#endif

        return STATUS_SUCCESS;
    }

    NoInline NTSTATUS CopyTrampolineStub(PBYTE& Trampoline, PVOID Address, ULONG_PTR CopyOpSize)
    {
        ULONG_PTR SourceOpLength, ForwardSize, BackwardSize;
        PBYTE Source = (PBYTE)Address;

        ForwardSize  = CopyOpSize;
        BackwardSize = 0;

        for (LONG_PTR Bytes = CopyOpSize; Bytes > 0; Bytes -= SourceOpLength)
        {
            ULONG_PTR TargetOpLength;

            if (Source[0] == 0xC2 || Source[0] == 0xC3)
                return STATUS_BUFFER_TOO_SMALL;

            CopyOneOpCode(Trampoline, Source, &TargetOpLength, &SourceOpLength, ForwardSize, BackwardSize);

            BackwardSize+= SourceOpLength;
            ForwardSize -= SourceOpLength;
            Trampoline  += TargetOpLength;
            Source      += SourceOpLength;
        }

        return STATUS_SUCCESS;
    }

    NoInline NTSTATUS GetHookAddressAndSize(PVOID Address, ULONG_PTR HookOpSize, PVOID *FinalAddress, PULONG_PTR Size)
    {
        ULONG_PTR   Length, OpSize;
        PBYTE       Buffer;

        OpSize = 0;
        Buffer = (PBYTE)Address;
        *FinalAddress = Address;

#if ML_X86 || ML_AMD64

        while (OpSize < HookOpSize)
        {
            if (Buffer[0] == 0xC2 || Buffer[0] == 0xC3)
                return STATUS_BUFFER_TOO_SMALL;

            Length  = GetOpCodeSize(Buffer);

            // jmp short const
            if (Buffer[0] == 0xEB)
            {
                if (OpSize == 0)
                {
                    if (Buffer[1] != 0)
                    {
                        Buffer += *(PCHAR)&Buffer[1] + Length;
                        *FinalAddress = Buffer;
                        continue;
                    }
                }
                else if (OpSize < HookOpSize - Length)
                {
                    return STATUS_BUFFER_TOO_SMALL;
                }
            }
            else if (Buffer[0] == 0xFF && Buffer[1] == 0x25)
            {
                // jmp [rimm]

#if ML_X86
                if (OpSize != 0 && OpSize + Length < HookOpSize)
                    return STATUS_BUFFER_TOO_SMALL;

                if (OpSize == 0 && HookOpSize > Length)
                {
                    Buffer = **(PBYTE **)&Buffer[2];
                    *FinalAddress = Buffer;
                    continue;
                }
#elif ML_AMD64
                if (OpSize != 0)
                    return STATUS_BUFFER_TOO_SMALL;

                // The RIP-relative displacement is a signed 32-bit value.
                // Sign-extend it before 64-bit pointer arithmetic; otherwise a
                // negative displacement is interpreted as roughly +4 GiB.
                Buffer = *(PBYTE *)(Buffer + Length + (LONG_PTR)*(PLONG)&Buffer[2]);
                *FinalAddress = Buffer;
                continue;
#endif // arch

            }

            Buffer += Length;
            OpSize += Length;
        }

#else

#error "not support"

#endif // arch

        *Size = OpSize;

        return STATUS_SUCCESS;
    }

    NoInline ULONG_PTR GetSizeOfHookOpSize(ULONG_PTR HookOp)
    {
        ULONG HookOp32 = (ULONG)HookOp;

#if ML_AMD64

        switch (HookOp32)
        {
            case OpCall:
            case OpJump:
                //
                // E8/E9 rel32
                //
                return 1 + sizeof(LONG);

            case OpJumpIndirect:
                //
                // FF 25 00 00 00 00 jmp qword ptr [rip + 0]
                // 00 00 00 00 00 00 00 00
                //
                return 0xE;

            case OpPush:
                // 68 00 00 00 80       push    80000000h
                // C7 04 24 00 00 00 80 mov     dword ptr [rsp], 80000000h
                // C3                   ret
                return 0xD;

            //case OpJumpIndirect:
            //    //
            //    // FF 25 00 00 00 00 jmp [rimm]
            //    // 00 00 00 00 00 00 00 00
            //    //
            //    break;
            //    //return 0xE;

            default:
                if (HookOp32 >= OpJRax && HookOp32 <= OpJRdi)
                {
                    //
                    // 48 B8 00 00 00 80 00 00 00 80    mov     rax, 8000000080000000h
                    // FF E0                            jmp     rax
                    //
                    return 0xC;
                }

                if (HookOp32 == OpJR10 || HookOp32 == OpJR11)
                {
                    //
                    // 49 BA/BB imm64   mov r10/r11, imm64
                    // 41 FF E2/E3      jmp r10/r11
                    //
                    return 0xD;
                }
        }

#elif ML_X86

        switch (HookOp)
        {
            case OpPush:
                //
                // 68 00000000  push const
                // C3           ret
                //
                return 1 + POINTER_SIZE + 1;

            case OpCall:
            case OpJump:
                //
                // E8 00000000  call    const
                // E9 00000000  jmp     const
                //
                return 1 + POINTER_SIZE;

            default:
                if (HookOp >= OpJRax && HookOp <= OpJRdi)
                {
                    //
                    // b8 88888888      mov     r32, const
                    // ffe0             jmp     r32
                    //
                    return 1 + POINTER_SIZE + 2;
                }
        }

#endif

        return ULONG_PTR_MAX;
    }

    ForceInline
    NTSTATUS
    ProtectMemory(
        PVOID       BaseAddress,
        ULONG_PTR   Size,
        ULONG       NewProtect,
        PULONG      OldProtect
    )
    {
        return Mm::ProtectMemory(CurrentProcess, BaseAddress, Size, NewProtect, OldProtect);
    }

    ForceInline
    NTSTATUS
    FlushInstructionCache(
        PVOID       BaseAddress,
        ULONG_PTR   NumberOfBytesToFlush
    )
    {
        return NtFlushInstructionCache(CurrentProcess, BaseAddress, NumberOfBytesToFlush);
    }

    PTRAMPOLINE_DATA AllocateTrampolineData()
    {
        return (PTRAMPOLINE_DATA)RtlAllocateHeap(this->ExecutableHeap, HEAP_ZERO_MEMORY, sizeof(TRAMPOLINE_DATA));
    }

    VOID FreeTrampolineData(PTRAMPOLINE_DATA TrampolineData)
    {
        if (TrampolineData != nullptr && TrampolineData->Relay != nullptr)
        {
            Mm::FreeVirtualMemory(TrampolineData->Relay);
            TrampolineData->Relay = nullptr;
        }

        if (TrampolineData != nullptr)
            RtlFreeHeap(this->ExecutableHeap, 0, TrampolineData);
    }

    NTSTATUS RemoveTrampolineData(PTRAMPOLINE_DATA TrampolineData)
    {
        NTSTATUS Status;
        ULONG_PTR Index;

        Index = this->TrampolineList.IndexOf(TrampolineData);
        if (Index == this->TrampolineList.kInvalidIndex)
            return STATUS_NOT_FOUND;

        this->TrampolineList.Remove(Index);
        FreeTrampolineData(TrampolineData);

        return STATUS_SUCCESS;
    }

    DECL_NORETURN VOID RaiseDebugBreak()
    {
        __debugbreak();
    }

public:
    NoInline NTSTATUS PatchMemory(PPATCH_MEMORY_DATA PatchData, ULONG_PTR PatchCount, PVOID BaseAddress)
    {
        NTSTATUS Status;

        FOR_EACH(PatchData, PatchData, PatchCount)
        {
            switch (PatchData->PatchType)
            {
                case PatchMemoryTypes::MemoryPatch:
                    Status = HandleMemoryPatch(PatchData, BaseAddress);
                    break;

                case PatchMemoryTypes::FunctionPatch:
                    Status = HandleFunctionPatch(PatchData, BaseAddress);
                    break;

                default:
                    Status = STATUS_NOT_IMPLEMENTED;
                    break;
            }

            FAIL_RETURN(Status);
        }

        return STATUS_SUCCESS;
    }

    NTSTATUS RestoreMemory(PTRAMPOLINE_DATA TrampolineData)
    {
        NTSTATUS Status;
        PATCH_MEMORY_DATA PatchData = MemoryPatchVa(&TrampolineData->OriginalCode, TrampolineData->OriginSize, TrampolineData->PatchData.Function.Source);
        Status = HandleMemoryPatch(&PatchData, nullptr);
        NT_SUCCESS(Status) && RemoveTrampolineData(TrampolineData);

        return Status;
    }

public:
    static NoInline NTSTATUS CreateInstance(MemoryPatchManager **Manager)
    {
        lep::InitializeSupportHeap();

        *Manager = (MemoryPatchManager *)AllocateMemory(sizeof(**Manager));
        if (*Manager == nullptr)
            return STATUS_NO_MEMORY;

        ZeroMemory(*Manager, sizeof(**Manager));
        new (*Manager) MemoryPatchManager;

        return STATUS_SUCCESS;
    }

    static NoInline NTSTATUS DestroyInstance(MemoryPatchManager *Manager)
    {
        if (Manager != nullptr)
        {
            Manager->~MemoryPatchManager();
            FreeMemory(Manager);
        }

        return STATUS_SUCCESS;
    }
};

ForceInline MemoryPatchManager*& MemoryPatchManagerInstance()
{
    static MemoryPatchManager *Manager;
    return Manager;
}

NTSTATUS MP_CALL PatchMemory(PPATCH_MEMORY_DATA PatchData, ULONG_PTR PatchCount, PVOID BaseAddress)
{
    NTSTATUS Status;

    if (MemoryPatchManagerInstance() == nullptr)
    {
        Status = MemoryPatchManager::CreateInstance(&MemoryPatchManagerInstance());
        FAIL_RETURN(Status);
    }

    return MemoryPatchManagerInstance()->PatchMemory(PatchData, PatchCount, BaseAddress);
}

NTSTATUS MP_CALL RestoreMemory(PTRAMPOLINE_DATA TrampolineData)
{
    if (MemoryPatchManagerInstance() == nullptr)
        return STATUS_FLT_NOT_INITIALIZED;

    return MemoryPatchManagerInstance()->RestoreMemory(TrampolineData);
}

ML_NAMESPACE_END_(Mp);

#pragma pop_macro("POINTER_SIZE")

#endif
