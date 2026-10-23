#include "../Support.h"
#if CPP_DEFINED

PIMAGE_RESOURCE_DATA_ENTRY
Nt_FindResource(
    PVOID   Module,
    PCWSTR  Name,
    PCWSTR  Type
)
{
    return ml::Native::Ldr::FindResource(Module, Name, Type);
}

PVOID
Nt_LoadResource(
    PVOID                       Module,
    PIMAGE_RESOURCE_DATA_ENTRY  ResourceDataEntry,
    PULONG                      Size
)
{
    return ml::Native::Ldr::LoadResource(Module, ResourceDataEntry, Size);
}

NTSTATUS
Nt_AllocateMemory(
    HANDLE      ProcessHandle,
    PVOID*      BaseAddress,
    ULONG_PTR   Size,
    ULONG       Protect,        /* = PAGE_EXECUTE_READWRITE */
    ULONG       AllocationType  /* = MEM_RESERVE | MEM_COMMIT */
)
{
    return ml::Native::Mm::AllocVirtualMemory(BaseAddress, Size, Protect, AllocationType, ProcessHandle);
}

NTSTATUS
Nt_FreeMemory(
    HANDLE  ProcessHandle,
    PVOID   BaseAddress
)
{
    return ml::Native::Mm::FreeVirtualMemory(BaseAddress, ProcessHandle);
}


#endif
