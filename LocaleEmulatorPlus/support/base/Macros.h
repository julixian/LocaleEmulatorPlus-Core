#ifndef _MACROS_H_6cd488df_3b68_4112_a937_9fcfa63eb723
#define _MACROS_H_6cd488df_3b68_4112_a937_9fcfa63eb723

#ifndef _TYPES_H_54e4879c_024a_491f_b990_859f407442fe
#define _TYPES_H_54e4879c_024a_491f_b990_859f407442fe

#ifndef _CONFIG_H_2a678057_bcdf_4132_9bc3_d6048ac24434
#define _CONFIG_H_2a678057_bcdf_4132_9bc3_d6048ac24434

#if (defined(_MSC_VER) && _MSC_VER >= 1300)
    #pragma once
#endif

#if defined(__INTEL_COMPILER)
    #define ML_COMPILER_INTEL __INTEL_COMPILER
    #define ML_COMPILER_MSC 1700
    #define MY_COMPILER_MSC ML_COMPILER_MSC

#elif defined(_MSC_VER)
    #define ML_COMPILER_MSC _MSC_VER
    #define MY_COMPILER_MSC ML_COMPILER_MSC

#endif // compiler

#if ML_COMPILER_MSC >= 1500
    #define MY_MSC_1500 1
    #define SUPPORT_VA_ARGS_MACRO 1
#endif  // MY_COMPILER_MSC >= 1500

#if ML_COMPILER_MSC >= 1700
    #define ML_SUPPORT_LAMBDA 1
#endif // ML_SUPPORT_LAMBDA

#if defined(UNICODE) || defined(_UNICODE)
    #define MY_UNICODE_ENABLE 1
#else
    #define MY_UNICODE_ENABLE 0
#endif // set unicode flag

#if !defined(WINCE)
    #define MY_OS_WIN32 1
#elif defined(_WIN32_WCE)
    #define MY_OS_WINCE 1
#endif // MY_OS


#if MY_OS_WIN32

#if !defined(_WIN32_WINNT)
    #define _WIN32_WINNT 0x0503
#endif /* _WIN32_WINNT */

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && defined(_M_IX86)
    #define _X86_
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && defined(_M_AMD64)
    #define _AMD64_
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && defined(_M_M68K)
    #define _68K_
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && defined(_M_MPPC)
    #define _MPPC_
#endif

#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_M_IX86) && !defined(_AMD64_) && defined(_M_IA64)
#if !defined(_IA64_)
    #define _IA64_
#endif /* !_IA64_ */
#endif

#ifndef _MAC
#if defined(_68K_) || defined(_MPPC_)
    #define _MAC
#endif
#endif /* _MAC */

#if defined(_AMD64_)

    #define MY_X64 1
    #define ML_AMD64    1

#elif defined(_X86_)

    #define ML_X86 1
    #define ML_I386 1

#endif

#endif // MY_OS_WIN32

/************************************************************************/
/*  compiler settings                                                   */
/************************************************************************/
#pragma warning(disable:4530 4100 4101 4731 4740 4146 4214 4201 4750 4457 4458 4477 4459 4456)

#if MY_MSC_1500
    #pragma warning(suppress:4530)
#endif  // MY_MSC_1500

#if defined(ML_COMPILER_INTEL)
    #pragma warning(disable:7 157 170 181 186 869 1899 2557)
#endif

#if MY_COMPILER_MSC < 1300
    #define for if(1) for
#endif /* fix vc6 for bug */

#ifndef CPP_DEFINED
    #if defined(__cplusplus)
        #define CPP_DEFINED 1
    #else
        #define CPP_DEFINED 0
    #endif
#endif // CPP_DEFINED

#if defined(__cplusplus_cli)
    #define CPP_CLI_DEFINED 1
#else
    #define CPP_CLI_DEFINED 0
#endif // cpp cli

#if !ML_DISABLE_THIRD_LIB

    #define INCLUDE_LIB(_lib) comment(lib, #_lib)

#else

    #define INCLUDE_LIB(_lib) once

#endif // ML_DISABLE_THIRD_LIB

#define USE_STACK_FRAME_POINTER   "y", off
#define UNUSE_STACK_FRAME_POINTER "y", on

#if !defined(IF_EXIST)
    #define IF_EXIST __if_exists
#endif /* IF_EXIST */

#if !defined(IF_NOT_EXIST)
    #define IF_NOT_EXIST __if_not_exists
#endif /* IF_NOT_EXIST */

#if !defined(typedef_s)
    #define typedef_s(s, d) IF_NOT_EXIST(d) { typedef s d; }

    #if SUPPORT_VA_ARGS_MACRO
        #define typedef_s2(s, ...) IF_NOT_EXIST(d) { typedef s __VA_ARGS__; }
    #endif  // SUPPORT_VA_ARGS_MACRO

#endif /* typedef_s */

#ifndef EXTCPP
    #if CPP_DEFINED
        #define EXTCPP extern "C++"
    #else
        #define EXTCPP
    #endif
#endif /* EXTCPP */

#ifndef NOVTABLE
    #define NOVTABLE __declspec(novtable)
#endif /* NOVTABLE */

#if ML_AMD64

#ifndef ASM
    #define ASM
#endif /* ASM */

#ifndef NAKED
    #define NAKED
#endif /* ASM */

#if !defined(INLINE_ASM)
    #if defined(ML_COMPILER_INTEL)
        #define INLINE_ASM __asm
    #else
        #define INLINE_ASM
    #endif
#endif /* INLINE_ASM */

#else // x86

#ifndef ASM
    #define ASM __declspec(naked)
#endif /* ASM */

#ifndef NAKED
    #define NAKED __declspec(naked)
#endif /* ASM */

#if !defined(INLINE_ASM)
    #define INLINE_ASM __asm
#endif /* INLINE_ASM */

#endif // x64 x86

#ifndef MY_DLL_EXPORT
    #define MY_DLL_EXPORT __declspec(dllexport)
#endif  /* MY_DLL_EXPORT */

#ifdef _MY_NTLIB_DLL_
    #define MY_NTLIB_DLL_EXPORT __declspec(dllexport)
#else
    #define MY_NTLIB_DLL_EXPORT
#endif  /* _MY_NTLIB_DLL_ */

#ifndef MY_DLL_IMPORT
    #define MY_DLL_IMPORT __declspec(dllimport)
#endif /* MY_DLL_IMPORT */

#if !defined(MY_CRT_IMPORT)
    #if defined(_DLL)
        #define MY_IMPORT MY_DLL_IMPORT
    #else
        #define MY_IMPORT
    #endif
#endif /* MY_CRT_IMPORT */

#define MY_LIB_NAME MY_LIB

/* C++ 下的函数修饰 */
#if  CPP_DEFINED
    #define _ML_C_HEAD_     extern "C" {
    #define _ML_C_TAIL_     }
    #define _ML_CPP_HEAD_     extern "C++" {
    #define _ML_CPP_TAIL_     }
    #define ML_NAMESPACE_BEGIN(name) namespace name {
    #define ML_NAMESPACE_END }
    #define ML_NAMESPACE_END_(name) }
    #define DEFAULT_VALUE(type, var, value) type var = value
#else
    #define _ML_C_HEAD_
    #define _ML_C_TAIL_
    #define _ML_CPP_HEAD_
    #define _ML_CPP_TAIL_
    #define ML_NAMESPACE_BEGIN(name)
    #define ML_NAMESPACE_END
    #define DEFAULT_VALUE(type, var, value) type var
#endif /* _ML_C_HEAD_ */

#define ML_NAMESPACE ML_NAMESPACE_BEGIN(ml)

#define MY_NAMESPACE_BEGIN  ML_NAMESPACE_BEGIN
#define MY_NAMESPACE_END    ML_NAMESPACE_END

#ifndef ForceInline
    #if (MY_COMPILER_MSC >= 1200)
        #define ForceInline __forceinline
    #else
        #define ForceInline __inline
    #endif
#endif /* ForceInline */

#ifndef NoInline
    #if (MY_COMPILER_MSC >= 1300)
        #define NoInline __declspec(noinline)
    #else
        #define NoInline
    #endif
#endif /* NoInline */

#if (defined(_M_IX86) || defined(_M_IA64) || defined(_M_AMD64)) && !defined(MIDL_PASS)
    #define DECL_IMPORT __declspec(dllimport)
    #define DECL_EXPORT __declspec(dllexport)
#else
    #define DECL_IMPORT
    #define DECL_EXPORT
#endif

#if !defined(DISABLE_SEH)

    #define SEH_TRY     __try
    #define SEH_FINALLY __finally
    #define SEH_LEAVE   __leave

    #if SUPPORT_VA_ARGS_MACRO
        #define SEH_EXCEPT(...)  __except(__VA_ARGS__)
    #else
        #define SEH_EXCEPT  __except
    #endif  // SUPPORT_VA_ARGS_MACRO

#else

    #define SEH_TRY
    #define SEH_FINALLY
    #define SEH_LEAVE

    #if SUPPORT_VA_ARGS_MACRO
        #define SEH_EXCEPT(...) for (int __c_false = 0; __c_false; )
    #else
        #define SEH_EXCEPT()
    #endif  // SUPPORT_VA_ARGS_MACRO

#endif // DISABLE_SEH

#ifndef EXTC
    #if CPP_DEFINED
        #define EXTC extern "C"
        #define EXTC_IMPORT EXTC __declspec(dllimport)
        #define EXTC_EXPORT EXTC DECL_EXPORT
    #else
        #define EXTC
        #define EXTC_IMPORT __declspec(dllimport)
    #endif
#endif /* EXTC */

#ifndef DECL_NORETURN
    #if (MY_COMPILER_MSC >= 1200) && !defined(MIDL_PASS)
        #define DECL_NORETURN   __declspec(noreturn)
    #else
        #define DECL_NORETURN
    #endif
#endif

#ifndef DECL_NOTHROW
    #if (MY_COMPILER_MSC >= 1200) && !defined(MIDL_PASS)
        #define DECL_NOTHROW   __declspec(nothrow)
    #else
        #define DECL_NOTHROW
    #endif
#endif

#ifndef DECL_ALIGN
    #if (MY_COMPILER_MSC >= 1300) && !defined(MIDL_PASS)
        #define DECL_ALIGN(x)   __declspec(align(x))
    #else
        #define DECL_ALIGN(x)
    #endif
#endif

#ifndef SYSTEM_CACHE_ALIGNMENT_SIZE
    #if defined(_AMD64_) || defined(_X86_)
        #define SYSTEM_CACHE_ALIGNMENT_SIZE 64
    #else
        #define SYSTEM_CACHE_ALIGNMENT_SIZE 128
    #endif
#endif

#ifndef DECL_CACHEALIGN
    #define DECL_CACHEALIGN DECL_ALIGN(SYSTEM_CACHE_ALIGNMENT_SIZE)
#endif

#ifndef DECL_UUID
    #if (MY_COMPILER_MSC >= 1100) && CPP_DEFINED
        #define DECL_UUID(x)    __declspec(uuid(x))
    #else
        #define DECL_UUID(x)
    #endif
#endif

#ifndef DECL_NOVTABLE
    #if (MY_COMPILER_MSC >= 1100) && CPP_DEFINED
        #define DECL_NOVTABLE   __declspec(novtable)
    #else
        #define DECL_NOVTABLE
    #endif
#endif

#ifndef DECL_SELECTANY
    #if (MY_COMPILER_MSC >= 1100)
        #define DECL_SELECTANY  __declspec(selectany)
    #else
        #define DECL_SELECTANY
    #endif
#endif

#ifndef NOP_FUNCTION
    #if (MY_COMPILER_MSC >= 1210)
        #define NOP_FUNCTION __noop
    #else
        #define NOP_FUNCTION (void)0
    #endif
#endif

#ifndef DECL_ADDRSAFE
    #if (MY_COMPILER_MSC >= 1200) && (defined(_M_ALPHA) || defined(_M_AXP64))
        #define DECL_ADDRSAFE  __declspec(address_safe)
    #else
        #define DECL_ADDRSAFE
    #endif
#endif

#ifndef DECL_DEPRECATED
    #if (MY_COMPILER_MSC >= 1300) && !defined(MIDL_PASS)
        #define DECL_DEPRECATED   __declspec(deprecated)
        #define DEPRECATE_SUPPORTED
    #else
        #define DECL_DEPRECATED
        #undef  DEPRECATE_SUPPORTED
    #endif
#endif

#if !defined(DECL_THREAD) && (defined(MY_COMPILER_MSC) || defined(__INTEL_COMPILER))
    #define DECL_THREAD __declspec(thread)
#endif // DECL_THREAD

#ifndef UNUSED_VARIABLE
    #define UNUSED_VARIABLE(Var) IF_EXIST(Var) { (Var); }
#endif // UNUSED_VARIABLE

#ifndef True
    #define True 1l
#endif /* True */

#ifndef False
    #define False 0l
#endif /* False */

#ifndef NULL
    #define NULL 0l
#endif /* NULL */

#ifdef CDECL
    #undef CDECL
#endif /* CDECL */
#define CDECL __cdecl

#ifdef STDCALL
    #undef STDCALL
#endif /* STDCALL */
#define STDCALL __stdcall

#ifdef FASTCALL
    #undef FASTCALL
#endif /* STDCALL */

#if ML_AMD64 || CPP_CLI_DEFINED
    #define FASTCALL
#else
    #define FASTCALL __fastcall
#endif

#ifdef THISCALL
    #undef THISCALL
#endif

#ifdef ML_COMPILER_MSC
    #define THISCALL __thiscall
#else
    #define THISCALL
#endif

#ifndef OPTIONAL
    #define OPTIONAL
#endif /* OPTIONAL */

#ifndef RESERVED
    #define RESERVED
#endif /* RESERVED */


#if !defined(ARGUMENT_PRESENT)
    #define ARGUMENT_PRESENT(ArgumentPointer) ((CHAR *)((ULONG_PTR)(ArgumentPointer)) != (CHAR *)(NULL) )
    #define ARGUMENT_NOT_PRESENT(ArgumentPointer) (!ARGUMENT_PRESENT(ArgumentPointer))
#endif



#if ML_COMPILER_MSC

#define PROPERTY(type, name) \
    __declspec(property(put = __property_set_##name, get = __property_get_##name ) ) type name; \
    typedef type __property_type_##name

#define READONLY_PROPERTY(type, name) \
    __declspec(property(get = __property_get_##name) ) type name; \
    typedef type __property_type_##name; \
    GET(name)

#define WRITEONLY_PROPERTY(type, name) \
    __declspec(property(put = __property_set_##name) ) type name; \
    typedef type property__tmp_type_##name; \
    SET(name)

#define GET(name) __property_type_##name __property_get_##name()
#define SET(name) void __property_set_##name(const __property_type_##name & value)

#endif // msc


#if !defined(INTEL_STATIC)
    #if defined(__INTEL_COMPILER)
        #define INTEL_STATIC static
    #else
        #define INTEL_STATIC
    #endif
#endif /* INTEL_STATIC */

#if !defined(MY_W64)
    #if !defined(__midl) && (defined(_X86_) || defined(_M_IX86)) && (_MSC_VER >= 1300)
        #define MY_W64 __w64
    #else
        #define MY_W64
    #endif
#endif

#endif // _CONFIG_H_2a678057_bcdf_4132_9bc3_d6048ac24434

//#pragma warning(disable:4201)

/************************************************************************/
/*  类型                                                                */
/************************************************************************/

_ML_C_HEAD_

#if !defined(_WCHAR_T_DEFINED)
    typedef unsigned short wchar_t;
    #define _WCHAR_T_DEFINED
#endif

#if !CPP_DEFINED

#define nullptr NULL

#endif // cpp

typedef float               Float, *PFloat, *LPFloat;
typedef double              Double, *PDouble, *LPDouble;

typedef char                Char, s8, Int8, *ps8, *PInt8, *PChar;
typedef const char         *PCChar;
typedef wchar_t             WChar, *PWChar;
typedef const wchar_t      *PCWChar;
typedef unsigned char       u8, UChar, UInt8, UInt24, Byte, *pu8, *PByte, *PUInt8, *PUChar;
typedef short               Short, s16, Int16, *ps16, *PInt16, *PShort, *LPShort;
typedef unsigned short      UShort, Word, u16, UInt16, *pu16, *PUInt16, *PWord, *LPWord, *PUShort, *LPUShort;
typedef long                Long, *PLong, *LPLong;
typedef long long           Long64, LongLong, *PLong64, *PLongLong;
typedef unsigned long       Dword, ULong, *PULong, *LPULong, *PDword, *LPDword;
typedef unsigned long long  ULong64, ULongLong, *PULong64, *PULongLong;
typedef void                Void, *PVoid, *LPVoid;
typedef const void         *LPCVoid, *PCVoid;

typedef int                 Bool, Int, s32, Int32, *PInt, *ps32, *PInt32;
typedef unsigned int        UInt, u32, UInt32, *PUInt, *pu32, *PUInt32;

typedef __int64             s64, Int64, *ps64, *PInt64;
typedef unsigned __int64    u64, UInt64, *pu64, *PUInt64;

typedef int (__cdecl *QSORT_COMPARE_ROUTINE)(const void *,const void *);

#if CPP_DEFINED

EXTCPP
template<typename T>
class LambdaCastHelper
{
public:

    typedef T FUNC;
};

#endif // CPP_DEFINED

#define QSortCallbackM(...) (QSORT_COMPARE_ROUTINE)(LambdaCastHelper<int(CDECL *)(__VA_ARGS__)>::FUNC)[] (__VA_ARGS__) -> int


#pragma pack(push, 1)

typedef struct
{
    UShort Offset   : 12;
    UShort Type     : 4;

} IMAGE_RELOCATION_ADDRESS_ENTRY, *PIMAGE_RELOCATION_ADDRESS_ENTRY;

typedef struct
{
    ULong   VirtualAddress;
    ULong   SizeOfBlock;
    IMAGE_RELOCATION_ADDRESS_ENTRY TypeOffset[1];

} *PIMAGE_BASE_RELOCATION2;

#pragma pack(pop)

#if 0 && CPP_DEFINED

class UInt24
{
protected:
    UInt32 m_Value;

public:
    template<class Type>
    UInt24& operator=(Type Value)
    {
        m_Value = (UInt32)Value & 0x00FFFFFF;
        return m_Value;
    }

    template<class Type>
    UInt24& operator+(Type Value)
    {
        m_Value = (m_Value + Value) & 0x00FFFFFF;
    }
};

#endif // CPP_DEFINED


#if defined(MY_X64)
    typedef __int64 Int_Ptr, *PInt_Ptr;
    typedef unsigned __int64 UInt_Ptr, *PUInt_Ptr;

    typedef __int64 Long_Ptr, *PLong_Ptr, LongPtr, *PLongPtr;
    typedef unsigned __int64 ULong_Ptr, *PULong_Ptr, ULongPtr, *PULongPtr;
#else
    typedef int MY_W64 Int_Ptr, *PInt_Ptr;
    typedef unsigned int MY_W64 UInt_Ptr, *PUInt_Ptr;

    typedef long MY_W64 Long_Ptr, *PLong_Ptr, LongPtr, *PLongPtr;
    typedef unsigned long MY_W64 ULong_Ptr, *PULong_Ptr, ULongPtr, *PULongPtr;
#endif

typedef ULong_Ptr SizeT, *PSizeT;
typedef Long_Ptr  SSizeT, *PSSizeT;

#define MAX_SHORT  (Short) (0x7FFF)
#define MAX_USHORT (UShort)(0xFFFF)
#define MAX_INT    (Int)   (0x7FFFFFFF)
#define MAX_UINT   (UInt)  (0xFFFFFFFF)
#define MAX_INT64  (Int64) (0x7FFFFFFFFFFFFFFF)
#define MAX_UINT64 (UInt64)(0xFFFFFFFFFFFFFFFF)
#define MAX_NTPATH  0x220

#if CPP_DEFINED

IF_NOT_EXIST(_PVFV)
{
    typedef void (CDECL *_PVFV)(void);
    typedef int  (CDECL *_PIFV)(void);
    typedef void (CDECL *_PVFI)(int);
}

EXTC _PVFV __xi_a, __xi_z, __xc_a, __xc_z;

inline void my_initterm(_PVFV *pfbegin, _PVFV *pfend)
{
    /*
    * walk the table of function pointers from the bottom up, until
    * the end is encountered.  Do not skip the first entry.  The initial
    * value of pfbegin points to the first valid entry.  Do not try to
    * execute what pfend points to.  Only entries before pfend are valid.
    */
    while ( pfbegin < pfend )
    {
        /*
        * if current table entry is non-NULL, call thru it.
        */
        if ( *pfbegin != NULL )
            (**pfbegin)();
        ++pfbegin;
    }
}

#endif // CPP_DEFINED

#define DR7_LEN_1_BYTE  1
#define DR7_LEN_2_BYTE  2
#define DR7_LEN_4_BYTE  3
#define DR7_RW_EXECUTE  0
#define DR7_RW_WRITE    1
#define DR7_RW_ACCESS   3

#pragma pack(1)

typedef union
{
    ULong Dr6;
    struct
    {
        Byte B0         : 1;
        Byte B1         : 1;
        Byte B2         : 1;
        Byte B3         : 1;
        Byte Reserve1   : 8;
        Byte Reserve2   : 2;
        Byte BD         : 1;
        Byte BS         : 1;
        Byte BT         : 1;
    };

} DR6_INFO, *PDR6_INFO;

typedef union
{
    ULong Dr7;
    struct
    {
        Byte L0         : 1;    // 0x01
        Byte G0         : 1;    // 0x02
        Byte L1         : 1;    // 0x03
        Byte G1         : 1;    // 0x04
        Byte L2         : 1;    // 0x05
        Byte G2         : 1;    // 0x06
        Byte L3         : 1;    // 0x07
        Byte G3         : 1;    // 0x08
        Byte LEP         : 1;    // 0x09
        Byte GE         : 1;    // 0x0A
        Byte Reserve    : 3;    // 0x0B
        Byte GD         : 1;    // 0x0E
        Byte Reserve2   : 2;    // 0x0F
        Byte RW0        : 2;    // 0x11
        Byte LepN0       : 2;    // 0x13
        Byte RW1        : 2;    // 0x15
        Byte LepN1       : 2;    // 0x17
        Byte RW2        : 2;    // 0x19
        Byte LepN2       : 2;    // 0x1B
        Byte RW3        : 2;    // 0x1D
        Byte LepN3       : 2;    // 0x1F
    };
} DR7_INFO, *PDR7_INFO;

typedef struct
{
    ULong_Ptr Rdi;
    ULong_Ptr Rsi;
    ULong_Ptr Rbp;
    ULong_Ptr Rsp;
    ULong_Ptr Rbx;
    ULong_Ptr Rdx;
    ULong_Ptr Rcx;
    ULong_Ptr Rax;

} PUSHAD_REGISTER, *PPUSHAD_REGISTER;

typedef union
{
    struct
    {
        ULong LowPart;
        Long  HighPart;
    };
    Long64 QuadPart;
} Large_Integer, *PLarge_Integer, *LPLarge_Integer;

typedef union
{
    struct
    {
        ULong LowPart;
        ULong HighPart;
    };
    ULong64 QuadPart;
} ULarge_Integer, *PULarge_Integer, *LPULarge_Integer;

typedef union
{
    struct
    {
        UInt16 w1;
        UInt16 w2;
        UInt16 w3;
        UInt16 w4;
        UInt16 w5;
        UInt16 w6;
        UInt16 w7;
        Int16  w8;
    };

    struct
    {
        UInt32 d1;
        UInt32 d2;
        UInt32 d3;
        Int32  d4;
    };

    struct
    {
        ULarge_Integer q1;
        Large_Integer  q2;
    };
} MMXRegister;

typedef union
{
    LPVoid  lpBuffer;
    PByte   pbBuffer;
    PUInt16 pBuffer16;
    PUInt32 pBuffer32;
    PUInt64 pBuffer64;
    PLarge_Integer  pLi;
    PULarge_Integer pULi;
} MultiTypePointer;

#pragma pack()

#if !CPP_DEFINED
    typedef unsigned char bool, *pbool;
    #define inline __inline
#else
    typedef bool *pbool;
#endif /* __cplusplus */

#if MY_UNICODE_ENABLE
    typedef WChar TChar;
#else
    typedef Char TChar;
#endif /* UNICODE */

typedef TChar *PTChar;
typedef const TChar *PCTChar;

#if MY_OS_WIN32 && !ML_AMD64

ForceInline Void FASTCALL DebugBreakPoint()
{
    __debugbreak();
}

ForceInline Int CDECL _purecall2()
{
    DebugBreakPoint();
    return 0;
}

ForceInline int CDECL _purecall()
{
    DebugBreakPoint();
    return 0;
}

#endif

// override global operator new/delete

#if !defined(MYAPI)
    #define MYAPI(funcname) funcname
#endif


    #if USE_CRT_ALLOC
        #define MY_OPNEW_ALLOC(n) malloc(n)
        #define MY_OPNEW_FREE(p)  free(p)
    #else
        #define MY_OPNEW_ALLOC(n) HeapAlloc(GetProcessHeap(), 0, n)
        #define MY_OPNEW_FREE(p)  HeapFree(GetProcessHeap(), 0, p)
    #endif

        #define NEW_ALLOC_API   RtlAllocateHeap
        #define NEW_FREE_API    RtlFreeHeap


#ifndef SAFE_DELETE
    #define SAFE_DELETE(p) if (p) \
                           { \
                               delete (p); \
                               (p) = NULL; \
                           }
#endif // SAFE_DELETE

#ifndef SAFE_FREE
    #define SAFE_FREE(p) if (p) \
                           { \
                               MY_OPNEW_FREE(p); \
                               (p) = NULL; \
                           }
#endif // SAFE_FREE

#if CPP_DEFINED

// && (defined(_INC_MALLOC) || defined(_WINDOWS_))

#define OVERLOAD_OP_NEW  \
    inline Void* CDECL operator new[](size_t size)\
    {\
        return size ? MY_OPNEW_ALLOC(size) : NULL;\
    }\
    \
    inline Void* CDECL operator new(size_t size)\
    {\
        return (Void *)(new char[size]);\
    }\
    \
    inline Void CDECL operator delete(Void *p)\
    {\
        if (p != NULL) \
            MY_OPNEW_FREE(p);\
    }\
    \
    inline Void CDECL operator delete[](Void *p)\
    {\
        delete(p);\
    }

#define OVERLOAD_CPP_NEW_WITH_HEAP(NewHeap) \
    inline PVoid CDECL operator new[](size_t Size) \
    { \
        return NEW_ALLOC_API((NewHeap), 0, Size); \
    } \
    inline PVoid CDECL operator new(size_t Size) \
    { \
        return operator new[](Size); \
    } \
    inline Void CDECL operator delete(PVoid pBuffer) \
    { \
        if (pBuffer != NULL) \
            NEW_FREE_API((NewHeap), 0, pBuffer); \
    } \
    inline Void CDECL operator delete[](PVoid pBuffer) \
    { \
        delete pBuffer; \
    }

#define OVERLOAD_CPP_METHOD_NEW_WITH_HEAP(NewHeap) \
    inline PVoid CDECL operator new[](size_t Size) \
    { \
        return NEW_ALLOC_API((NewHeap), 0, Size); \
    } \
    inline PVoid CDECL operator new(size_t Size) \
    { \
        return operator new[](Size); \
    } \
    inline Void CDECL operator delete(PVoid pBuffer) \
    { \
        if (pBuffer != NULL) \
            NEW_FREE_API((NewHeap), 0, pBuffer); \
    } \
    inline Void CDECL operator delete[](PVoid pBuffer) \
    { \
        delete pBuffer; \
    }

#else

    #define OVERLOAD_OP_NEW
    #define OVERLOAD_CPP_NEW_WITH_HEAP(Heap)
    #define OVERLOAD_CPP_METHOD_NEW_WITH_HEAP(Heap)

#endif /* __cplusplus */


#define ML_OVERLOAD_NEW \
            inline PVoid CDECL operator new[](size_t Size) \
            { \
                return AllocateMemoryP(Size); \
            } \
            inline PVoid CDECL operator new(size_t Size) \
            { \
                return operator new[](Size); \
            } \
            inline Void CDECL operator delete(PVoid Buffer) \
            { \
                if (Buffer != NULL) \
                    FreeMemoryP(Buffer); \
            } \
            inline Void CDECL operator delete(PVoid Buffer, UInt_Ptr) \
            { \
                if (Buffer != NULL) \
                    FreeMemoryP(Buffer); \
            } \
            inline Void CDECL operator delete[](PVoid Buffer) \
            { \
                delete Buffer; \
            } \
            inline Void CDECL operator delete[](PVoid Buffer, UInt_Ptr) \
            { \
                delete Buffer; \
            }

_ML_C_TAIL_

//#pragma warning(default:4201)

#endif // _TYPES_H_54e4879c_024a_491f_b990_859f407442fe

#if ML_DEBUG_KERNEL

#if ML_X86

    #define DEBUG_BREAK_POINT     DbgBreakPoint

#elif ML_AMD64

    #define DEBUG_BREAK_POINT     DbgBreakPoint

#endif // arch

    #if SUPPORT_VA_ARGS_MACRO
        #define DebugPrint(...)         DbgPrint(__VA_ARGS__)
    #else
        #define DebugPrint  DbgPrint
    #endif

#else

    #define DEBUG_BREAK_POINT()

    #if SUPPORT_VA_ARGS_MACRO
        #define DebugPrint(...)
    #else
        #define DebugPrint()
    #endif

#endif /* ML_DEBUG_KERNEL */

#define BP DEBUG_BREAK_POINT

#define RTL_CONST_STRING(_str, _string) \
            (_str).Length = sizeof(_string) - sizeof((_str).Buffer[0]); \
            (_str).MaximumLength = sizeof(_string); \
            (_str).Buffer = (_string);

enum // ECodePage
{
    CP_SHIFTJIS = 932,
    CP_GBK      = 936,
    CP_GB2312   = CP_GBK,
    CP_BIG5     = 950,
    CP_UTF16_LE = 1200,
    CP_UTF16_BE = 1201,
};

#define BOM_UTF8        (UInt32)(0xBFBBEF)
#define BOM_UTF16_LE    (UInt16)(0xFEFF)
#define BOM_UTF16_BE    (UInt16)(0xFFFE)

#define __MAKE_WSTRING(str) L##str
#define MAKE_WSTRING(str) __MAKE_WSTRING(str)

#define ASM_UNIQUE() INLINE_ASM mov eax, __LINE__

#define ASM_DUMMY(Bytes) ASM_DUMMY_##Bytes

#define ASM_DUMMY_1 INLINE_ASM nop
#define ASM_DUMMY_2 INLINE_ASM mov eax, eax

// Lepa eax, [eax+0];
#define ASM_DUMMY_3 INLINE_ASM __emit 0x8D INLINE_ASM __emit 0x40 INLINE_ASM __emit 0x00

// // Lepa esi, [esi]
#define ASM_DUMMY_4 INLINE_ASM __emit 0x8D \
                    INLINE_ASM __emit 0x74 \
                    INLINE_ASM __emit 0x26 \
                    INLINE_ASM __emit 0x00

#define ASM_DUMMY_5 INLINE_ASM mov eax, 1
#define ASM_DUMMY_6 INLINE_ASM __emit 0x8D INLINE_ASM __emit 0x80 INLINE_ASM __emit 0x00 INLINE_ASM __emit 0x00 INLINE_ASM __emit 0x00 INLINE_ASM __emit 0x00

#define ASM_DUMMY_7 INLINE_ASM __emit 0x8D \
                    INLINE_ASM __emit 0xB4 \
                    INLINE_ASM __emit 0x26 \
                    INLINE_ASM __emit 0x00 \
                    INLINE_ASM __emit 0x00 \
                    INLINE_ASM __emit 0x00 \
                    INLINE_ASM __emit 0x00

#define ASM_DUMMY_AUTO() INLINE_ASM mov eax, 1 INLINE_ASM mov ecx, 1 INLINE_ASM mov edx, 1 ASM_UNIQUE() INLINE_ASM ret

#if !defined(_M_IA64)
    #define MEMORY_PAGE_SIZE (4 * 1024)
#else
    #define MEMORY_PAGE_SIZE (8 * 1024)
#endif

#define NO_BREAK

#define NT6_LIB(lib) "D:/Dev/Windows Kits/8.0/Lib/win8/um/x86/" #lib

#define LOOP_ALWAYS for (;;)
#define LOOP_FOREVER LOOP_ALWAYS
#define LOOP_ONCE   for (Bool __condition_ = True; __condition_; __condition_ = False)

#if !defined(BREAK_IF)
    #define BREAK_IF(c) if (c) break;
#endif /* BREAK_IF */

#if !defined(CONTINUE_IF)
    #define CONTINUE_IF(c) if (c) continue;
#endif /* CONTINUE_IF */

#if !defined(RETURN_IF)
    #define RETURN_IF(c, r) if (c) return r
#endif /* RETURN_IF */

#if !defined(countof)
    #define countof(x) (sizeof((x)) / sizeof(*(x)))
#endif /* countof */

#if !defined(CONST_STRLEN)
    #define CONST_STRLEN(str) (countof(str) - 1)
    #define CONST_STRSIZE(str) (CONST_STRLEN(str) * sizeof(str[0]))
#endif // CONST_STRLEN

#if !defined(bitsof)
    #define bitsof(x) (sizeof(x) * 8)
#endif /* bitsof */

#define FIELD_BASE(address, type, field) (type *)((ULONG_PTR)address - (ULONG_PTR)&((type *)0)->field)
#define FIELD_TYPE(_Type, _Field)  TYPE_OF(((_Type*)0)->_Field)

#ifndef FIELD_SIZE
    #define FIELD_SIZE(type, field) (sizeof(((type *)0)->field))
#endif // FIELD_SIZE

#define SET_FLAG(_V, _F)    ((_V) |= (_F))
#define CLEAR_FLAG(_V, _F)  ((_V) &= ~(_F))
#define FLAG_ON(_V, _F)     (!!((_V) & (_F)))
#define FLAG_OFF(_V, _F)     (!FLAG_ON(_V, _F))

#if !defined(TEST_BIT)
    #define TEST_BIT(value, bit) ((value) & (1 << bit))
#endif /* TEST_BIT */

#if !defined(TEST_BITS)
    #define TEST_BITS(value, bits) (Bool)(!!((value) & (bits)))
#endif /* TEST_BITS */

#if !defined(ROUND_DOWN)
    #define ROUND_DOWN(Value, Multiple) ((Value) / (Multiple) * (Multiple))
#endif /* ROUND_DOWN */

#if !defined(ROUND_UP)
    #define ROUND_UP(Value, Multiple) (ROUND_DOWN((Value) + (Multiple) - 1, (Multiple)))
#endif /* ROUND_UP */

#if !defined(IN_RANGE)
    #define IN_RANGE(low, value, high) (((low) <= (value)) && (value) <= (high))
    #define IN_RANGEEX(low, value, high) (((low) <= (value)) && (value) < (high))
#endif

#if !defined(MEMORY_PAGE_ADDRESS)
    #define MEMORY_PAGE_ADDRESS(Address) (ROUND_DOWN((ULongPtr)(Address), MEMORY_PAGE_SIZE))
#endif /* MEMORY_PAGE_ADDRESS */

#if defined(DEFAULT_VALUE)
    #undef DEFAULT_VALUE
#endif // DEFAULT_VALUE

#if CPP_DEFINED
    #define DEFAULT_VALUE(type, var, value) type var = value
    #define DEF_VAL(var, value)             var = value
#else
    #define DEFAULT_VALUE(type, var, value) type var
    #define DEF_VAL(var, value)             var
#endif //CPP_DEFINED

#if defined(__INTEL_COMPILER)
    #pragma warning(disable:1479)
#endif

#define MAKE_UNIQUE_NAME2(n) _##n
#define MAKE_UNIQUE_NAME(n) MAKE_UNIQUE_NAME2(n)
#define DUMMY_STRUCT(n) DECL_ALIGN(1) struct { char _[n]; } MAKE_UNIQUE_NAME(__LINE__)
// #define DUMMY_STRUCT(size) DECL_ALIGN(1) struct { Byte __dummy[size]; }


#define MAKE_NAME_PREFIX_(prefix, n) prefix##n
#define MAKE_NAME_PREFIX(prefix, n) MAKE_NAME_PREFIX_(prefix, n)

#define GetStructMemberOffset(type, member_begin, member_end) \
    (PByte)&(*(type*)0).member_end - (PByte)&(*(type*)0).member_begin

#if CPP_DEFINED

template<class FunctionType, class FunctionAddress>
ForceInline
FunctionType GetRoutinePtr(FunctionType, FunctionAddress Address)
{
    union
    {
        FunctionType    _Addr1;
        FunctionAddress _Addr2;
    };

    _Addr2 = Address;

    return _Addr1;
}

#if SUPPORT_VA_ARGS_MACRO

#define CallFuncPtr(RoutineType, RoutineAddr, ...) ((TYPE_OF(RoutineType)*)(RoutineAddr))(__VA_ARGS__)

#endif // SUPPORT_VA_ARGS_MACRO

#endif

//#if CPP_DEFINED
/*
template<class Type>
ForceInline const Type& MY_MAX(const Type &Value1, const Type &Value2)
{
    return Value1 < Value2 ? Value2 : Value1;
}

template<class Type>
ForceInline const Type& MY_MIN(const Type &Value1, const Type &Value2)
{
    return Value2 < Value1 ? Value2 : Value1;
}
*/
//#else // CPP_DEFINED

#define ML_MAX(a, b) ((a) < (b) ? (b) : (a))
#define ML_MIN(a, b) ((a) < (b) ? (a) : (b))

#define MY_MAX ML_MAX
#define MY_MIN ML_MIN

//#endif // CPP_DEFINED

#define CHAR_UPPER(ch) (IN_RANGE('a', (ch), 'z') ? ((ch) & (Byte)0xDF) : ch)
#define CHAR_LOWER(ch) (IN_RANGE('A', (ch), 'Z') ? ((ch) | (Byte)~0xDF) : ch)

#define _CHAR_UPPER4W(ch) (UInt64)((ch) & 0xFFDFFFDFFFDFFFDF)
#define CHAR_UPPER4W(ch) _CHAR_UPPER4W((UInt64)(ch))
#define CHAR_UPPER3W(ch) (UInt64)(CHAR_UPPER4W(ch) & 0x0000FFFFFFFFFFFF)
#define CHAR_UPPER2W(ch) (UInt64)(CHAR_UPPER4W(ch) & 0x00000000FFFFFFFF)
#define CHAR_UPPER1W(ch) (UInt64)(CHAR_UPPER4W(ch) & 0x000000000000FFFF)

#define _CHAR_UPPER4(ch) (UInt32)((ch) & 0xDFDFDFDF)
#define CHAR_UPPER4(ch) (UInt32)_CHAR_UPPER4((UInt32)(ch))
#define CHAR_UPPER3(ch) (UInt32)(CHAR_UPPER4(ch) & 0x00FFFFFF)
#define CHAR_UPPER2(ch) (UInt32)(CHAR_UPPER4(ch) & 0x0000FFFF)
#define CHAR_UPPER1(ch) (UInt32)(CHAR_UPPER4(ch) & 0x000000FF)
#define CHAR_UPPER8(ch) ((UInt64)(ch) & 0xDFDFDFDFDFDFDFDF)

#define _TAG2(s) ((((s) << 8) | ((s) >> 8)) & 0xFFFF)
#define TAG2(s) _TAG2((u16)(s))

#define _TAG3(s) ( \
                (((s) >> 16) & 0xFF)       | \
                (((s)        & 0xFF00))    | \
                (((s) << 16) & 0x00FF0000) \
                )
#define TAG3(s) _TAG3((u32)(s))

#define _TAG4(s) ( \
                (((s) >> 24) & 0xFF)       | \
                (((s) >> 8 ) & 0xFF00)     | \
                (((s) << 24) & 0xFF000000) | \
                (((s) << 8 ) & 0x00FF0000) \
                )
#define TAG4(s) _TAG4((u32)(s))

#define TAG8(Lepft, right) (((UInt64)TAG4(right) << 32) | TAG4(Lepft))

#define _TAG2W(x) (((x) & 0xFF) << 16 | ((x) & 0xFF00) >> 8)
#define TAG2W(x) (UInt32)_TAG2W((UInt32)(x))

#define _TAG3W(x) (TAG4W(x) >> 16)
#define TAG3W(x) (UInt64)_TAG3W((UInt64)(x))

#define _TAG4W(x) (((UInt64)TAG2W((x) & 0xFFFF) << 32) | ((UInt64)TAG2W((x) >> 16)))
#define TAG4W(x) (UInt64)_TAG4W((UInt64)(x))

#pragma warning(disable:4310)
#define SWAP2(v) (u16)(((u32)(v) << 8) | ((u16)(v) >> 8))
#define SWAPCHAR(v) ((u32)SWAP2(v))

#define LoByte(v)  (u8) ((v & 0xFF))
#define HiByte(v)  (u8) (((v) >> 8) & 0xFF)
#define LoWord(v)  (u16)((v) & 0xFFFF)
#define HiWord(v)  (u16)(((v) >> 16) & 0xFFFF)
#define LoDword(v) (u32)((v))
#define HiDword(v) (u32)(((v) >> 32))

#define MakeLong(l, h)   (long)((s32)(l) | ((s32)(h) << 16))
#define MakeLong64(l, h) (s64)((s64)(l) | (s64)(h) << 32)

#define MakeDword(l, h) (u32)((u32)(l) | ((u32)(h) << 16))
#define MakeQword(l, h) (u64)((u64)(l) | (u64)(h) << 32)

#define MAKEINTRESA(i) ((PChar)(Word)(i))
#define MAKEINTRESW(i) ((PWChar)(Word)(i))

#define STRTOULONG(x) (ULong_Ptr)(x)

#define ML_IP_ADDRESS(a1, a2, a3, a4) ((a1) | ((a2) << 8) | ((a3) << 16) | ((a4) << 24))
#define ML_PORT(_port) SWAP2(_port)

#define FOR_EACH(_it, _base, _n) for (auto _Count = ( ((_it) = (_base)), (_n)); _Count != 0; ++(_it), --_Count)
#define FOR_EACH_REVERSE(_it, _base, _n) for (auto _Count = ( ((_it) = (_base) + (_n) - 1), (_n)); _Count != 0; --(_it), --_Count)
#define FOR_EACH_ARRAY(_it, _arr) FOR_EACH(_it, _arr, countof(_arr))
#define FOR_EACH_S(_it, _base, _n, _size) for (auto _Count = ( ((_it) = (_base)), (_n)); _Count != 0; ((_it) = PtrAdd(_it, _size)), --_Count)
#define FOR_EACH_X(_it, _base, _n) for (auto _Count = ( ((_it) = (_base)), (_n); _Count != 0; ++(_it), --(_n), --_Count)

#define FOR_EACH_FORWARD(_it, _n) { (_it) += (_n); (_Count) += (_n); }
#define FOR_EACH_BACKWARD(_it, _n) { (_it) -= (_n); (_Count) -= (_n); }

#if MY_UNICODE_ENABLE
    #define MAKEINTRES MAKEINTRESW
#else
    #define MAKEINTRES MAKEINTRESA
#endif /* UNICODE */


#define DEFINED_HRESULT(failed, facility, code) \
            ((HRESULT)(((failed) << 31) | ((facility) << 16) | (code)))

#define SET_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        EXTC const GUID DECLSPEC_SELECTANY name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

#define DECL_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        EXTC const GUID FAR name

#endif // _MACROS_H_6cd488df_3b68_4112_a937_9fcfa63eb723
