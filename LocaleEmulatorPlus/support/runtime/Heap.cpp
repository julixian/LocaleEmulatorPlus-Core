#include "../Support.h"
_ML_C_HEAD_


PVOID AllocateMemoryP(ULONG_PTR Size, ULONG Flags)
{
    return MemoryAllocator::AllocateMemory(Size, Flags);
}

PVOID ReAllocateMemoryP(PVOID Memory, ULONG_PTR Size, ULONG Flags)
{
    return MemoryAllocator::ReAllocateMemory(Memory, Size, Flags);
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
    return MemoryAllocator::FreeMemory(Memory, Flags);
}

PVOID AllocateVirtualMemory(ULONG_PTR Size, ULONG Protect, HANDLE ProcessHandle)
{
    PVOID BaseAddress = NULL;
    NTSTATUS Status;
    Status = Nt_AllocateMemory(ProcessHandle, &BaseAddress, Size, Protect);
    return NT_SUCCESS(Status) ? BaseAddress : NULL;
}

BOOL FreeVirtualMemory(PVOID Memory, HANDLE ProcessHandle)
{
    return NT_SUCCESS(Nt_FreeMemory(ProcessHandle, Memory));
}


_ML_C_TAIL_
