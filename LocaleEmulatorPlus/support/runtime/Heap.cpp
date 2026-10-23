#include "../Support.h"
_ML_C_HEAD_


PVOID AllocateMemoryP(ULONG_PTR Size, ULONG Flags)
{
    return HeapAllocator::AllocateMemory(Size, Flags);
}

PVOID ReAllocateMemoryP(PVOID Memory, ULONG_PTR Size, ULONG Flags)
{
    return HeapAllocator::ReAllocateMemory(Memory, Size, Flags);
}

PVOID AllocateMemory(ULONG_PTR Size, ULONG Flags)
{
    return RtlAllocateHeap(CurrentPeb()->ProcessHeap, Flags, Size);
}

PVOID ReAllocateMemory(PVOID Memory, ULONG_PTR Size, ULONG Flags)
{
    PVOID Block;

    if (Memory == NULL)
        return AllocateMemory(Size, Flags);

    Block = RtlReAllocateHeap(CurrentPeb()->ProcessHeap, Flags, Memory, Size);

    if (Block == nullptr)
    {
        FreeMemory(Memory);
        return nullptr;
    }

    return Block;
}

BOOL FreeMemory(PVOID Memory, ULONG Flags)
{
    return Memory != NULL ? RtlFreeHeap(CurrentPeb()->ProcessHeap, Flags, Memory) : FALSE;
}

BOOL FreeMemoryP(PVOID Memory, ULONG Flags)
{
    return HeapAllocator::FreeMemory(Memory, Flags);
}

PVOID AllocateVirtualMemory(ULONG_PTR Size, ULONG Protect, HANDLE ProcessHandle)
{
    PVOID BaseAddress = NULL;
    NTSTATUS Status;
    Status = lep::Native::Mm::AllocVirtualMemory(&BaseAddress, Size, Protect, MEM_RESERVE | MEM_COMMIT, ProcessHandle);
    return NT_SUCCESS(Status) ? BaseAddress : NULL;
}

BOOL FreeVirtualMemory(PVOID Memory, HANDLE ProcessHandle)
{
    return NT_SUCCESS(lep::Native::Mm::FreeVirtualMemory(Memory, ProcessHandle));
}


_ML_C_TAIL_

    ML_NAMESPACE

    LONG_PTR InitializeSupportHeap()
    {
        if (HeapAllocator::GetGlobalHeap() == NULL && HeapAllocator::CreateGlobalHeap() == nullptr)
            return STATUS_NO_MEMORY;

        return STATUS_SUCCESS;
    }

    LONG_PTR UninitializeSupportHeap()
    {
        HeapAllocator::DestroyGlobalHeap();

        return 0;
    }

    MY_NAMESPACE_END
