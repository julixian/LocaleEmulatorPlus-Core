#include "../base/Platform.h"

#ifndef _MEMORY_H_38900f05_7a34_48ee_8cd6_84300c9b8f1f
#define _MEMORY_H_38900f05_7a34_48ee_8cd6_84300c9b8f1f

#pragma warning(disable:4127)
#pragma warning(disable:4047)
#pragma warning(disable:4706)

#ifndef _INTRINSIC_H_725e6954_2a61_467e_9a57_c634deb44f80
#define _INTRINSIC_H_725e6954_2a61_467e_9a57_c634deb44f80


_ML_C_HEAD_

// #include <emmintrin.h>
#include <xmmintrin.h>
#include <crtversion.h>

#if _VC_CRT_BUILD_VERSION >= 23026
    #include <intrin.h>
#endif

void __cdecl _disable(void);
void __cdecl _enable(void);

void*   __cdecl _alloca(size_t _Size);
int     __cdecl memcmp(const void *buf1, const void *buf2, size_t count);
void*   __cdecl memcpy(void *dest, const void *src, size_t count);
void*   __cdecl memset(void *dest, int c, size_t count);

unsigned char   __cdecl _rotr8(unsigned char value, unsigned char shift);
unsigned short  __cdecl _rotr16(unsigned short value, unsigned char shift);
unsigned char   __cdecl _rotl8(unsigned char value, unsigned char shift);
unsigned short  __cdecl _rotl16(unsigned short value, unsigned char shift);

void            __cdecl __debugbreak();

void*           _AddressOfReturnAddress();
void*           _ReturnAddress();

#if CPP_DEFINED

IF_NOT_EXIST(__stosb)
{
    void            __cdecl __stosb(void* Dest, unsigned char Data, size_t Count);
    void            __cdecl __stosw(void* Dest, unsigned short Data, size_t Count);
    void            __cdecl __stosd(void* Dest, unsigned long Data, size_t Count);
}

IF_NOT_EXIST(__movsb)
{
    void            __cdecl __movsb(void *Destination, const void *Source, size_t Count);
    void            __cdecl __movsw(void *Dest, const void *Source, size_t Count);
    void            __cdecl __movsd(void *Dest, const void *Source, size_t Count);
}

#endif


/************************************************************************
  interlocked
************************************************************************/

long            __cdecl _InterlockedIncrement(long volatile * lpAddend);
long            __cdecl _InterlockedDecrement(long volatile * lpAddend);
long            __cdecl _InterlockedCompareExchange(long volatile * Destination, long Exchange, long Comperand);
long            __cdecl _InterlockedExchangeAdd(long volatile * Addend, long Value);
long            __cdecl _InterlockedExchange(volatile long * Target, long Value);

long            _InterlockedAnd(long volatile *Destination, long Value);
long            _InterlockedOr(long volatile *Destination, long Value);
long            _InterlockedXor(long volatile *Destination, long Value);

long            __cdecl _InterlockedIncrement_acq(long * lpAddend);
long            __cdecl _InterlockedIncrement_rel(long * lpAddend);
short           _InterlockedIncrement16(short volatile * lpAddend);
short           __cdecl _InterlockedIncrement16_acq(short * lpAddend);
short           __cdecl _InterlockedIncrement16_rel(short * lpAddend);
__int64         _InterlockedIncrement64(volatile __int64 * lpAddend);
__int64         __cdecl _InterlockedIncrement64_acq(volatile __int64 * lpAddend);
__int64         __cdecl _InterlockedIncrement64_rel(volatile __int64 * lpAddend);

long            __cdecl _InterlockedDecrement_acq(long * lpAddend);
long            __cdecl _InterlockedDecrement_acq(long * lpAddend);
short           _InterlockedDecrement16(short volatile * lpAddend);
short           __cdecl _InterlockedDecrement16_acq(short * lpAddend);
short           __cdecl _InterlockedDecrement16_rel(short * lpAddend);
__int64         _InterlockedDecrement64(volatile __int64 * lpAddend);
__int64         __cdecl _InterlockedDecrement64_acq(__int64 * lpAddend);
__int64         __cdecl _InterlockedDecrement64_rel(__int64 * lpAddend);

long            __cdecl _InterlockedExchange_acq(long * Target, long Value);
__int64         _InterlockedExchange64(volatile __int64 * Target, __int64 Value);
__int64         __cdecl _InterlockedExchange64_acq(__int64 * Target, __int64 Value);

long            __cdecl _InterlockedCompareExchange_acq(long volatile * Destination, long Exchange, long Comperand);
long            __cdecl _InterlockedCompareExchange_rel(long volatile * Destination, long Exchange, long Comperand);
short                   _InterlockedCompareExchange16(short volatile * Destination, short Exchange, short Comperand);
short           __cdecl _InterlockedCompareExchange16_acq(short volatile * Destination, short Exchange, short Comperand);
short           __cdecl _InterlockedCompareExchange16_rel(short volatile * Destination, short Exchange, short Comperand);
__int64         __cdecl _InterlockedCompareExchange64_acq(__int64 volatile * Destination, __int64 Exchange, __int64 Comperand);
__int64         __cdecl _InterlockedCompareExchange64_rel(__int64 volatile * Destination, __int64 Exchange, __int64 Comperand);
unsigned char   __cdecl _InterlockedCompareExchange128(__int64 volatile * Destination, __int64 ExchangeHigh, __int64 ExchangeLow, __int64 * Comparand);
unsigned char           _BitScanForward (unsigned long *Index, unsigned long Mask);


void _ReadBarrier(void);
void _WriteBarrier(void);
void _ReadWriteBarrier(void);


/************************************************************************
  cpuid
************************************************************************/
void __cpuid(int CPUInfo[4], int InfoType);
void __cpuidex(int CPUInfo[4], int InfoType, int ECXValue);


/************************************************************************
  dr
************************************************************************/
UInt_Ptr __readdr(unsigned int DebugRegister);

#if ML_AMD64
    void __writedr(unsigned DebugRegister, unsigned __int64 DebugValue);
#elif ML_X86
    void __writedr(unsigned int DebugRegister, unsigned int DebugValue);
#endif

/************************************************************************
  crx
************************************************************************/

#if ML_AMD64

    void __writecr0(unsigned __int64 Data);
    void writecr3(unsigned __int64 Data);
    void writecr4(unsigned __int64 Data);
    void writecr8(unsigned __int64 Data);

#elif ML_X86
    void __writecr0(unsigned Data);
    void writecr3(unsigned Data);
    void writecr4(unsigned Data);
    void writecr8(unsigned Data);
#endif

ULongPtr __readcr0(void);


/************************************************************************
  msr
************************************************************************/

unsigned __int64 __readmsr(unsigned long register);
void __writemsr(unsigned long Register, unsigned __int64 Value);


/************************************************************************
  vmx
************************************************************************/

void __vmx_off();


/************************************************************************
  idt
************************************************************************/

void __lidt(void *);


#if ML_AMD64

unsigned char __vmx_on(unsigned __int64 *VmsSupportPhysicalAddress);
unsigned char __vmx_vmread(size_t Field, size_t *FieldValue);
unsigned char __vmx_vmwrite(size_t Field, size_t FieldValue);
unsigned char __vmx_vmclear(unsigned __int64 *VmcsPhysicalAddress);
unsigned char __vmx_vmlaunch(void);
unsigned char __vmx_vmptrld(unsigned __int64 *VmcsPhysicalAddress);
void          __vmx_vmptrst(unsigned __int64 *VmcsPhysicalAddress);

#endif

#if !ML_AMD64

    #define _InterlockedCompareExchangePointer(Target, Exchange, Comperand) (PVOID)_InterlockedCompareExchange((long *)Target, (long)Exchange, (long)Comperand)

#ifndef _InterlockedExchangePointer
    #define _InterlockedExchangePointer(Target, Value) (PVOID)_InterlockedExchange((long *)Target, (long)Value)
#endif


    inline long long _InterlockedExchange64(long long volatile *Target, long long Value)
    {
        long long Old;

        do
        {
            Old = *Target;
        } while (_InterlockedCompareExchange64(Target, Value, Old) != Old);

        return Old;
    }


#else

    #define _InterlockedCompareExchangePointer(Target, Exchange, Comperand) (PVOID)_InterlockedCompareExchange64((__int64 *)Target, (__int64)Exchange, (__int64)Comperand)
    void * _InterlockedExchangePointer(void * volatile * Target, void * Value);
    void * _InterlockedExchangePointer_acq(void * volatile * Target, void * Value);

#endif

#if MY_OS_WIN32
    #define Interlocked_Increment _InterlockedIncrement
    #define Interlocked_Decrement _InterlockedDecrement
#else
    #define Interlocked_Increment InterlockedIncrement
    #define Interlocked_Decrement InterlockedDecrement
#endif // MY_OS_WIN32

ForceInline LongPtr _InterlockedIncrementPtr(PVoid Addend)
{
#if ML_AMD64
    return _InterlockedIncrement64((PLongPtr)Addend);
#else
    return _InterlockedIncrement((PLongPtr)Addend);
#endif
}

ForceInline LongPtr _InterlockedDecrementPtr(PVoid Addend)
{
#if ML_AMD64
    return _InterlockedDecrement64((PLongPtr)Addend);
#else
    return _InterlockedDecrement((PLongPtr)Addend);
#endif
}

ForceInline BOOLEAN MlBitScanForwardPtr(PULONG Index, ULONG_PTR Mask)
{
#if ML_AMD64

    return _BitScanForward64(Index, Mask);

#else

    return _BitScanForward(Index, Mask);

#endif
}

#pragma intrinsic(_BitScanForward)

_ML_C_TAIL_

#endif // _INTRINSIC_H_725e6954_2a61_467e_9a57_c634deb44f80

_ML_C_HEAD_

#define CopyStruct(dest, src, size) \
    { \
        typedef DECL_ALIGN(1) struct { ::Byte b[size]; } _SDummy; \
        *(_SDummy *)(dest) = *(_SDummy *)(src); \
    }

ForceInline void* AllocStack(size_t Size)
{
#if _DEBUG
    return malloc(ROUND_UP(Size, 16));
#else
    return _alloca(ROUND_UP(Size, 16));
#endif
}

inline PVoid XorMemory(PVoid Destination, Byte Value, size_t Size)
{
    UInt32 c1;
    PByte Buffer = (PByte)Destination;

    c1 = Value | (UInt32)(Value << 8) | (UInt32)(Value << 16) | (UInt32)(Value << 24);

    for (; Size != 0 && (Int_Ptr)Buffer & 3; --Size)
        *Buffer++ ^= c1;

    for (; Size >= 4; Buffer += 4, Size -= 4)
        *(PUInt32)Buffer ^= c1;

    for (; Size; --Size)
        *Buffer++ ^= c1;

    return Destination;
}

inline PVoid XorMemory4(PVoid Destination, UInt32 Value, size_t Size)
{
    PByte Buffer = (PByte)Destination;

    while (Size)
        *(PUInt32)&Buffer[Size -= 4] ^= Value;

    return Destination;
}

inline PVoid AndMemory(PVoid Destination, Byte Value, size_t Size)
{
    UInt32 c1;
    PByte  Buffer = (PByte)Destination;

    c1 = Value | (UInt32)(Value << 8) | (UInt32)(Value << 16) | (UInt32)(Value << 24);

    for (; Size != 0 && (Int_Ptr)Buffer & 3; --Size)
        *Buffer++ &= c1;

    for (; Size >= 4; Buffer += 4, Size -= 4)
        *(PUInt32)Buffer &= c1;

    for (; Size; --Size)
        *Buffer++ &= c1;

    return Destination;
}

inline PVoid OrMemory(PVoid Destination, Byte Value, size_t Size)
{
    UInt32 c1;
    PByte  Buffer = (PByte)Destination;

    c1 = Value | (UInt32)(Value << 8) | (UInt32)(Value << 16) | (UInt32)(Value << 24);

    for (; Size != 0 && (Int_Ptr)Buffer & 3; --Size)
        *Buffer++ |= c1;

    for (; Size >= 4; Buffer += 4, Size -= 4)
        *(PUInt32)Buffer |= c1;

    for (; Size; --Size)
        *Buffer++ |= c1;

    return Destination;
}

#if MY_OS_WIN32

#if !defined(MY_X64)

ForceInline LPVoid CDECL my_memcpy_inline(void *dst, const void *src, size_t count)
{
#if !MY_MSC_1500 || defined(MY_COMPILER_INTEL)
    __asm
    {
        mov  edi, dst;
        mov  esi, src;
        mov  ecx, count;
        mov  edx, ecx;
        shr  ecx, 2;
        rep  movsd;
        mov  ecx, edx;
        and  ecx, 3;
        rep  movsb;
    }
#else
    size_t n = count & ~3;
    __movsd((PULONG_PTR)dst, (const ULONG_PTR *)src, count >> 2);
    __movsb((PByte)dst + n, (PByte)src + n, count & 3);
#endif

    return dst;
}

ForceInline LPVoid CDECL my_memset_inline(void* dest, Byte c, size_t count)
{
    UInt32 c1;

    c1 = c | (UInt32)(c << 8);
    c1 |= (c1 << 16);
#if defined(MY_COMPILER_INTEL) || !MY_MSC_1500
    __asm
    {
        mov  edi, dest;
        mov  eax, c1;
        mov  ecx, count;
        mov  edx, ecx;
        shr  ecx, 2;
        rep  stosd;
        mov  ecx, edx;
        and  ecx, 3;
        rep  stosb;
    }
#else
    size_t n = count & ~3;
    __stosd((PULONG_PTR)dest, c1, count >> 2);
    __stosb((PByte)dest + n, (Byte)c1, count & 3);
#endif

	return dest;
}

ForceInline LPVoid CDECL memset4(void* dest, ULong_Ptr c, size_t count)
{
#if defined(MY_COMPILER_INTEL) || !defined(MY_COMPILER_MSC)
    __asm
    {
        mov  edi, dest;
        mov  eax, c;
        mov  ecx, count;
        mov  edx, ecx;
        shr  ecx, 2;
        rep  stosd;
        mov  ecx, edx;
        and  ecx, 3;
        rep  stosb;
    }
    return dest;
#else
    __stosd((PULONG_PTR)dest, (ULong)c, count / sizeof(c));
    return dest;
#endif
}

ForceInline LPVoid CDECL memset2(void* dest, UInt16 c, size_t count)
{
    UInt32 c1 = c | (c << 16);
    __asm
    {
        mov  edi, dest;
        mov  eax, c1;
        mov  ecx, count;
        mov  edx, ecx;
        shr  ecx, 2;
        rep  stosd;
        mov  ecx, edx;
        and  ecx, 3;
        rep  stosb;
    }

    return dest;
}

#endif // MY_X64

#endif // MY_OS_WIN32

// EXTC_IMPORT void* __cdecl memset(void * _Dst, int _Val, size_t _Size);
// EXTC_IMPORT void* __cdecl memcpy(void *dest, const void *src, size_t count);

#pragma intrinsic(memcmp, memcpy, memset)

#if !defined(NOT_USE_CUSTOM_MEMFUNC) && MY_OS_WIN32

#undef RtlMoveMemory

#if ML_AMD64

    #define CompareMemory(Buffer1, Buffer2, SizeInBytes) memcmp((void *)(Buffer1), (void *)(Buffer2), (size_t)(SizeInBytes))


#else //x86

#ifdef memcpy
    #undef memcpy
#endif /* memcpy */

#ifdef memset
    #undef memset
#endif /* memset */

#ifdef ZeroMemory
    #undef ZeroMemory
#endif /* ZeroMemory */

#ifdef CopyMemory
    #undef CopyMemory
#endif /* CopyMemory */

#ifdef RtlCopyMemory
    #undef RtlCopyMemory
#endif /* RtlCopyMemory */

#ifdef FillMemory
    #undef FillMemory
#endif // FillMemory

    #define memcpy my_memcpy_inline
    #define memset my_memset_inline
    #define CompareMemory(Buffer1, Buffer2, SizeInBytes) memcmp((void *)(Buffer1), (void *)(Buffer2), (size_t)(SizeInBytes))
    #define FillMemory(Destination, Length, Fill) memset(Destination, (Byte)Fill, Length)
    #define FillMemory4(Destination, Length, Fill) memset4(Destination, Fill, Length)
    #define ZeroMemory(Destination, Length) FillMemory(Destination, Length, 0)
    #define CopyMemory(Destination, Source, Length) memcpy((void *)(Destination), (void *)(Source), (size_t)(Length))
    #define RtlCopyMemory CopyMemory

#endif // arch

//    #define memcpy my_memcpy_detect
#endif // NOT_USE_CUSTOM_MEMFUNC

EXTC_IMPORT Void STDCALL RtlMoveMemory(PVoid Destination, PVoid Source, SizeT Length);

_ML_C_TAIL_



#pragma warning(default:4047)
#pragma warning(default:4127)
#pragma warning(default:4706)

#endif // _MEMORY_H_38900f05_7a34_48ee_8cd6_84300c9b8f1f
