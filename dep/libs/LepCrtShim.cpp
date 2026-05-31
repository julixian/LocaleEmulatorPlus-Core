extern "C" typedef char* va_list;
extern "C" typedef unsigned __int64 size_t;

extern "C" int __cdecl _vsnwprintf(wchar_t* buffer, size_t count, const wchar_t* format, va_list args);
extern "C" size_t __cdecl wcslen(const wchar_t* string);
extern "C" void* __stdcall RtlAllocateHeap(void* heap, unsigned long flags, size_t size);
extern "C" unsigned char __stdcall RtlFreeHeap(void* heap, unsigned long flags, void* block);
extern "C" void __stdcall RtlRaiseStatus(long status);
extern "C" unsigned __int64 __readgsqword(unsigned long offset);

#pragma function(wcslen)
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

static wchar_t LepToUpperW(wchar_t ch)
{
    return ch >= L'a' && ch <= L'z' ? ch - (L'a' - L'A') : ch;
}

static char LepToUpperA(char ch)
{
    return ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
}

extern "C" size_t __cdecl wcslen(const wchar_t* string)
{
    const wchar_t* cursor = string;
    if (cursor == 0)
        return 0;

    while (*cursor != 0)
        ++cursor;

    return (size_t)(cursor - string);
}

extern "C" int __cdecl _wcsicmp(const wchar_t* Lepft, const wchar_t* right)
{
    wchar_t l, r;

    if (Lepft == 0 || right == 0)
        return Lepft == right ? 0 : (Lepft == 0 ? -1 : 1);

    do
    {
        l = LepToUpperW(*Lepft++);
        r = LepToUpperW(*right++);
        if (l != r)
            return l < r ? -1 : 1;
    } while (l != 0);

    return 0;
}

extern "C" int __cdecl wcsicmp(const wchar_t* Lepft, const wchar_t* right)
{
    return _wcsicmp(Lepft, right);
}

extern "C" int __cdecl _stricmp(const char* Lepft, const char* right)
{
    char l, r;

    if (Lepft == 0 || right == 0)
        return Lepft == right ? 0 : (Lepft == 0 ? -1 : 1);

    do
    {
        l = LepToUpperA(*Lepft++);
        r = LepToUpperA(*right++);
        if (l != r)
            return l < r ? -1 : 1;
    } while (l != 0);

    return 0;
}

extern "C" int __cdecl strcmp(const char* left, const char* right)
{
    unsigned char l, r;

    if (left == 0 || right == 0)
        return left == right ? 0 : (left == 0 ? -1 : 1);

    do
    {
        l = (unsigned char)*left++;
        r = (unsigned char)*right++;
        if (l != r)
            return l < r ? -1 : 1;
    } while (l != 0);

    return 0;
}

extern "C" int __cdecl wcsncmp(const wchar_t* left, const wchar_t* right, size_t count)
{
    wchar_t l, r;

    if (left == 0 || right == 0)
        return left == right ? 0 : (left == 0 ? -1 : 1);

    while (count-- != 0)
    {
        l = *left++;
        r = *right++;
        if (l != r)
            return l < r ? -1 : 1;
        if (l == 0)
            return 0;
    }

    return 0;
}

extern "C" int __cdecl vswprintf(wchar_t* buffer, const wchar_t* format, va_list args)
{
    return _vsnwprintf(buffer, (size_t)-1, format, args);
}

extern "C" int __cdecl _vswprintf(wchar_t* buffer, const wchar_t* format, va_list args)
{
    return _vsnwprintf(buffer, (size_t)-1, format, args);
}

extern "C" int __cdecl _vscwprintf(const wchar_t* format, va_list args)
{
    wchar_t buffer[0x1000];
    return _vsnwprintf(buffer, (sizeof(buffer) / sizeof(buffer[0])) - 1, format, args);
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
