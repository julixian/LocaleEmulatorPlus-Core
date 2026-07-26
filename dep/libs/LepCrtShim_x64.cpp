extern "C" typedef char* va_list;
extern "C" typedef unsigned __int64 size_t;

extern "C" void* __stdcall RtlAllocateHeap(void* heap, unsigned long flags, size_t size);
extern "C" unsigned char __stdcall RtlFreeHeap(void* heap, unsigned long flags, void* block);
extern "C" void __stdcall RtlRaiseStatus(long status);
extern "C" unsigned __int64 __readgsqword(unsigned long offset);

#pragma intrinsic(__readgsqword)

static void* LepGetProcessHeap()
{
    unsigned char* peb = (unsigned char*)__readgsqword(0x60);
    return *(void**)(peb + 0x30);
}

static void LepZeroMemory(void* block, size_t size)
{
    unsigned char* cursor = (unsigned char*)block;

    while (size-- != 0)
        *cursor++ = 0;
}

extern "C" void __cdecl abort()
{
    RtlRaiseStatus((long)0xC0000409);

    for (;;)
    {
    }
}

extern "C" void __cdecl terminate()
{
    abort();
}

extern "C" void* __cdecl calloc(size_t count, size_t size)
{
    size_t total;
    void* block;

    if (count != 0 && size > ((size_t)-1) / count)
        return 0;

    total = count * size;
    block = RtlAllocateHeap(LepGetProcessHeap(), 0, total);
    if (block != 0)
        LepZeroMemory(block, total);

    return block;
}

extern "C" void __cdecl free(void* block)
{
    if (block != 0)
        RtlFreeHeap(LepGetProcessHeap(), 0, block);
}
