#ifndef _NTSTRING_H_9bfab99e_9722_48ad_9a23_53edcb64be76_
#define _NTSTRING_H_9bfab99e_9722_48ad_9a23_53edcb64be76_

#ifndef _STRINGHELPER_H_4eabe4cb_a327_45e5_9b83_14061dceb3c6
#define _STRINGHELPER_H_4eabe4cb_a327_45e5_9b83_14061dceb3c6

//#include "Algorithm.h"
#ifndef _KMP_H_56576ff0_9962_4584_aa21_3e68ef01d6ff_
#define _KMP_H_56576ff0_9962_4584_aa21_3e68ef01d6ff_


_ML_C_HEAD_

/************************************************************************/
/* KMP                                                                  */
/************************************************************************/
inline Long_Ptr InitNext(const Void *lpKeyWord, Long_Ptr KeyWordLen, PLong_Ptr piNext)
{
    Long_Ptr i, j;
    const Char *pDest;

    pDest = (const Char *)lpKeyWord;

    piNext[0] = -1;
    i = 0;
    j = (Long_Ptr)-1;

    while (i < KeyWordLen)
    {
        if (j == -1 || pDest[i] == pDest[j])
        {
            if (pDest[++i] != pDest[++j])
            {
                piNext[i] = j;
            }
            else
            {
                piNext[i] = piNext[j];
            }
        }
        else
        {
            j = piNext[j];
        }
    }

    return KeyWordLen;
}

inline PVoid KMP(PCVoid _Src, Long_Ptr SrcLength, PCVoid _KeyWord, Long_Ptr KeyWordLen)
{
    Long_Ptr i, j;
    PLong_Ptr  Next;//[100];
    PLong_Ptr pNext;
    const Char *pSrc, *pKeyWord;

    if (SrcLength == 0)
        return nullptr;

    Next = (PLong_Ptr)AllocStack((KeyWordLen + 1) * sizeof(*pNext));
    pNext  = Next;
    pSrc  = (const Char *)_Src;
    pKeyWord = (const Char *)_KeyWord;
    InitNext(_KeyWord, KeyWordLen, pNext);
    i = 0;
    j = 0;
    while (i != SrcLength && j != KeyWordLen)
    {
        if (j == -1 || pSrc[i] == pKeyWord[j])
        {
            ++i;
            ++j;
        }
        else
        {
            j = pNext[j];
        }
    }

    if (i == SrcLength && j != KeyWordLen)
        return nullptr;

    return (Void *)(pSrc + i - KeyWordLen);
}

_ML_C_TAIL_

#endif // _KMP_H_56576ff0_9962_4584_aa21_3e68ef01d6ff_
#include <stdarg.h>

#pragma warning(disable:4127)

_ML_C_HEAD_

PChar                StringLowerA(PChar pszString, ULong DEF_VAL(Length, -1));
PWChar               StringLowerW(PWChar pszString, ULong DEF_VAL(Length, -1));
PChar                StringUpperA(PChar pszString, ULong DEF_VAL(Length, -1));
PWChar               StringUpperW(PWChar pszString, ULong DEF_VAL(Length, -1));
Long_Ptr    FASTCALL StrLengthA(PCChar pString);
Long_Ptr    FASTCALL SSE2_StrLengthA(PCChar pString);
Long_Ptr    FASTCALL StrLengthW(PCWChar pString);
PChar       FASTCALL StrCopyA(PChar DestinationString, PCChar SourceString);
PWChar      FASTCALL StrCopyW(PWChar DestinationString, PCWChar SourceString);
PChar       FASTCALL StrConcatenateA(PChar DestinationString, PCChar SourceString);
PWChar      FASTCALL StrConcatenateW(PWChar DestinationString, PCWChar SourceString);
Long_Ptr    FASTCALL StrNCompareA(PCChar pString1, PCChar pString2, SizeT LengthToCompare);

PChar   FASTCALL findexta(PCChar lpFileName);
PWChar  FASTCALL findextw(PCWChar lpFileName);
PChar   FASTCALL findnamea(PCChar lpFileName);
PWChar  FASTCALL findnamew(PCWChar lpFileName, SizeT DEF_VAL(Length, -1));
PWChar           StrFindCharW(PCWChar lpString, WChar CharMatch);
PWChar           PeekLineW(PVoid pBuffer, PVoid pEndOfBuffer, PVoid pOutBuffer, PSizeT pcbOutBuffer);

inline PTChar StrCopy(LPVoid DestinationString, LPCVoid SourceString)
{
    return sizeof(*(PTChar)SourceString) == sizeof(Char) ? (PTChar)StrCopyA((PChar)DestinationString, (PCChar)SourceString) : (PTChar)StrCopyW((PWChar)DestinationString, (PCWChar)SourceString);
}

inline PTChar findext(LPVoid lpFileName)
{
    return sizeof(*(PTChar)lpFileName) == sizeof(Char) ? (PTChar)findexta((PCChar)lpFileName) : (PTChar)findextw((PCWChar)lpFileName);
}

inline PTChar findname(PCTChar lpFileName)
{
    return sizeof(*(PTChar)lpFileName) == 1 ? (PTChar)findnamea((PCChar)lpFileName) : (PTChar)findnamew((PCWChar)lpFileName, ULONG_PTR_MAX);
}

inline PChar FASTCALL rmexta(PChar pszPath)
{
    *findexta(pszPath) = 0;
    return pszPath;
}

inline PWChar FASTCALL rmextw(PWChar pszPath)
{
    *findextw(pszPath) = 0;
    return pszPath;
}

inline PTChar FASTCALL rmext(PTChar pszPath)
{
    if (sizeof(*pszPath) == sizeof(Char))
        return (PTChar)rmexta((PChar)pszPath);
    else
        return (PTChar)rmextw((PWChar)pszPath);
}

inline Void FASTCALL chext(Char *szFileName, Char *pExt)
{
    StrCopyA(findexta(szFileName), pExt);
}

inline Void FASTCALL chextw(WChar *szFileName, WChar *pExt)
{
    StrCopyW(findextw(szFileName), pExt);
}

inline PChar FASTCALL rmname(PChar pszPath)
{
    *findnamea(pszPath) = 0;
    return pszPath;
}

inline PWChar FASTCALL rmnamew(PWChar pszPath)
{
    *findnamew(pszPath, ULONG_PTR_MAX) = 0;
    return pszPath;
}

inline Long_Ptr FASTCALL StrLength(PCTChar pString)
{
    if (sizeof(TChar) == sizeof(Char))
        return StrLengthA((PCChar)pString);
    else
        return StrLengthW((PCWChar)pString);
}

enum
{
    StrCmp_Keep,
    StrCmp_ToLower,
    StrCmp_ToUpper,
};

Long_Ptr FASTCALL StrNICompareA(PCChar pString1, PCChar pString2, SizeT LengthToCompare, LongPtr DEF_VAL(CaseConvertTo, StrCmp_Keep));

inline Long_Ptr FASTCALL StrICompareA(PCChar pString1, PCChar pString2, LongPtr DEF_VAL(CaseConvertTo, StrCmp_Keep))
{
    return StrNICompareA(pString1, pString2, (SizeT)~0ull, CaseConvertTo);
}

Long_Ptr FASTCALL StrNICompareW(PCWChar pString1, PCWChar pString2, SizeT LengthToCompare, LongPtr DEF_VAL(CaseConvertTo, StrCmp_Keep));

inline Long_Ptr FASTCALL StrICompareW(PCWChar pString1, PCWChar pString2, LongPtr DEF_VAL(CaseConvertTo, StrCmp_Keep))
{
    return StrNICompareW(pString1, pString2, (SizeT)-1, CaseConvertTo);
}

inline Long_Ptr FASTCALL StrNICompare(PCTChar pString1, PCTChar pString2, SizeT LengthToCompare, LongPtr DEF_VAL(CaseConvertTo, StrCmp_Keep))
{
    if (sizeof(*pString1) == sizeof(Char))
    {
        return StrNICompareA((PCChar)pString1, (PCChar)pString2, LengthToCompare, CaseConvertTo);
    }
    else if (sizeof(*pString1) == sizeof(WChar))
    {
        return StrNICompareW((PCWChar)pString1, (PCWChar)pString2, LengthToCompare, CaseConvertTo);
    }

    return 0;
}

inline Long_Ptr FASTCALL StrICompare(PCTChar pString1, PCTChar pString2)
{
    return StrNICompare(pString1, pString2, (SizeT)-1, StrCmp_Keep);
}

inline Long_Ptr FASTCALL StrCompareA(PCChar pString1, PCChar pString2)
{
    return StrNCompareA(pString1, pString2, (SizeT)-1);
}

Long_Ptr FASTCALL StrNCompareW(PCWChar pString1, PCWChar pString2, SizeT LengthToCompare);

inline Long_Ptr FASTCALL StrCompareW(PCWChar pString1, PCWChar pString2)
{
    return StrNCompareW(pString1, pString2, (SizeT)-1);
}

inline Long_Ptr FASTCALL StrNCompare(PCTChar pString1, PCTChar pString2, SizeT LengthToCompare)
{
    if (sizeof(*pString1) == sizeof(Char))
    {
        return StrNCompareA((PCChar)pString1, (PCChar)pString2, LengthToCompare);
    }
    else if (sizeof(*pString1) == sizeof(WChar))
    {
        return StrNCompareW((PCWChar)pString1, (PCWChar)pString2, LengthToCompare);
    }

    return 0;
}

inline Long_Ptr FASTCALL StrCompare(PCTChar pString1, PCTChar pString2)
{
    return StrNCompare(pString1, pString2, (SizeT)-1);
}

inline PChar SubStrA(const Char *_Src, const Char *_Sub)
{
    return (Char *)KMP(_Src, StrLengthA(_Src), _Sub, StrLengthA(_Sub));
}

inline PWChar SubStrW(const WChar *_Src, const WChar *_Sub)
{
    return (PWChar)KMP(_Src, StrLengthW(_Src) * sizeof(WChar), _Sub, StrLengthW(_Sub) * sizeof(WChar));
}



Int32 StringToInt32A(PCChar pszString);
Int32 StringToInt32W(PCWChar pszString);

inline Int32 StringToInt32(PCTChar pszString)
{
    if (sizeof(*pszString) == sizeof(Char))
        return StringToInt32A((PChar)pszString);
    else
        return StringToInt32W((PWChar)pszString);
}

Int32 StringToInt32HexW(PCWChar pszString);
Int64 StringToInt64HexW(PCWChar pszString);
Int64 StringToInt64A(PChar pszString);
Int64 StringToInt64W(PCWChar pszString);

inline Int64 StringToInt64(PTChar pszString)
{
    if (sizeof(TChar) == sizeof(Char))
        return StringToInt64A((PChar)pszString);
    else if (sizeof(TChar) == sizeof(WChar))
        return StringToInt64W((PWChar)pszString);

    return 0;
}

Bool IsShiftJISChar(SizeT uChar);
Bool IsShiftJISString(PCChar pString, SizeT Length);

inline int GetFormatedSize(PCWSTR Format, ...)
{
    va_list Arguments;
    int Count;

    va_start(Arguments, Format);
    Count = _vscwprintf(Format, Arguments);
    va_end(Arguments);

    return Count;
}

Int FormatStringA  (PChar  pszBuffer, PCChar  pszFormat, ...);
Int FormatStringW  (PWChar pszBuffer, PCWChar pszFormat, ...);
Int FormatStringvA (PChar  pszBuffer, PCChar  pszFormat,  va_list args);
Int FormatStringvW (PWChar pszBuffer, PCWChar pszFormat,  va_list args);
Int FormatStringvnA(PChar  pszBuffer, UInt    cchLimitIn, PCChar  pszFormat, va_list args);
Int FormatStringvnW(PWChar pszBuffer, UInt    cchLimitIn, PCWChar pszFormat, va_list args);

// reactos\dll\win32\shlwapi\wsprintf.c

#define FMTSTR_FLAG_LEFTALIGN   0x00000001  /* Align output on the Lepft ('-' prefix) */
#define FMTSTR_FLAG_PREFIX_HEX  0x00000002  /* Prefix hex with 0x ('#' prefix) */
#define FMTSTR_FLAG_ZEROPAD     0x00000004  /* Pad with zeros ('0' prefix) */
#define FMTSTR_FLAG_LONG        0x00000008  /* Long arg ('l' prefix) */
#define FMTSTR_FLAG_SHORT       0x00000010  /* Short arg ('h' prefix) */
#define FMTSTR_FLAG_UPPER_HEX   0x00000020  /* Upper-case hex ('X' specifier) */
#define FMTSTR_FLAG_WIDE        0x00000040  /* Wide arg ('w' prefix) */
#define FMTSTR_FLAG_FORCESIGNED 0x00000080  /* Force signed ('+' prefix) */
#define FMTSTR_FLAG_I64         0x00000100  /* 64 bit version ("%I64" prefix) */

typedef enum
{
    FMTSTR_FORMAT_UNKNOWN,
    FMTSTR_FORMAT_CHAR,
    FMTSTR_FORMAT_WCHAR,
    FMTSTR_FORMAT_STRING,
    FMTSTR_FORMAT_WSTRING,
    FMTSTR_FORMAT_SIGNED,
    FMTSTR_FORMAT_UNSIGNED,
    FMTSTR_FORMAT_HEXA,
    FMTSTR_FORMAT_FLOAT,

#if MY_OS_WIN32
    FMTSTR_FORMAT_BINARY,
#endif
} FMTSTR_TYPE;

typedef struct
{
    UInt        flags;
    UInt        width;
    UInt        precision;
    FMTSTR_TYPE type;
} FMTSTR_FORMAT;

typedef union
{
    WChar   wchar_view;
    Char    char_view;
    PCChar  lpcstr_view;
    PCWChar lpcwstr_view;
    Int     int_view;
    Int64   int64_view;
} FMTSTR_DATA;

Bool
StrMatchExpression(
    IN PWChar   Expression,
    IN PWChar   Name,
    IN Bool     DEF_VAL(IgnoreCase, TRUE),
    IN PWChar   DEF_VAL(UpcaseTable, NULL) OPTIONAL
);

_ML_C_TAIL_

#if CPP_DEFINED

#endif // CPP_DEFINED

#pragma warning(default:4127)

#endif // _STRINGHELPER_H_4eabe4cb_a327_45e5_9b83_14061dceb3c6

#ifndef _NTIFS1_

NATIVE_API
NTSTATUS
NTAPI
RtlValidateUnicodeString(
    IN  ULONG Flags RESERVED,
    IN  PCUNICODE_STRING String
);

NATIVE_API
BOOLEAN
NTAPI
RtlCreateUnicodeString(
    OUT PUNICODE_STRING DestinationString,
    IN  PCWSTR          SourceString
);

NATIVE_API
VOID
NTAPI
RtlFreeAnsiString(
    PANSI_STRING AnsiString
);

NATIVE_API
VOID
NTAPI
RtlFreeUnicodeString(
    PUNICODE_STRING UnicodeString
);

NATIVE_API
VOID
NTAPI
RtlCopyUnicodeString(
    PUNICODE_STRING DestinationString,
    PCUNICODE_STRING SourceString
);

#define RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE             (0x00000001)
#define RTL_DUPLICATE_UNICODE_STRING_ALLOCATE_NULL_STRING       (0x00000002)
#define RTL_DUPSTR_ADD_NULL                                     RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE
#define RTL_DUPSTR_ALLOC_NULL                                   RTL_DUPLICATE_UNICODE_STRING_ALLOCATE_NULL_STRING

NATIVE_API
NTSTATUS
NTAPI
RtlDuplicateUnicodeString(
    ULONG               Flags,
    PCUNICODE_STRING    SourceUnicodeString,
    PUNICODE_STRING     DestinationUnicodeString
);

NATIVE_API
BOOLEAN
NTAPI
RtlEqualUnicodeString(
    IN  PCUNICODE_STRING    String1,
    IN  PCUNICODE_STRING    String2,
    IN  BOOLEAN             CaseInSensitive
);

NATIVE_API
LONG
NTAPI
RtlCompareUnicodeString(
    PCUNICODE_STRING    String1,
    PCUNICODE_STRING    String2,
    BOOLEAN             CaseInSensitive
);

NATIVE_API
NTSTATUS
NTAPI
RtlUpcaseUnicodeString(
    IN OUT  PUNICODE_STRING     DestinationString,
    IN      PCUNICODE_STRING    SourceString,
    IN      BOOLEAN             AllocateDestinationString
);

NATIVE_API
NTSTATUS
NTAPI
RtlDowncaseUnicodeString(
    IN OUT  PUNICODE_STRING     DestinationString,
    IN      PCUNICODE_STRING    SourceString,
    IN      BOOLEAN             AllocateDestinationString
);

NATIVE_API
NTSTATUS
NTAPI
RtlAppendUnicodeToString(
    IN OUT  PUNICODE_STRING Destination,
    IN      PCWSTR          Source
);

NATIVE_API
NTSTATUS
NTAPI
RtlAppendUnicodeStringToString(
    IN OUT  PUNICODE_STRING     Destination,
    IN      PCUNICODE_STRING    Source
);

NATIVE_API
PWSTR
NTAPI
RtlFindUnicodeSubstring(
    IN PUNICODE_STRING Source,
    IN PUNICODE_STRING Substring
);

NATIVE_API
NTSTATUS
NTAPI
RtlMultiAppendUnicodeStringBuffer(
    IN OUT  PUNICODE_STRING Destination,
    IN      ULONG_PTR       NumberOfStringBuffer,
    IN      PWSTR*          StringBuffers
);

NATIVE_API
NTSTATUS
NTAPI
RtlStringFromGUID(
    IN  REFGUID Guid,
    OUT PUNICODE_STRING GuidString
);

NATIVE_API
NTSTATUS
NTAPI
RtlIntegerToUnicodeString(
    IN      ULONG           Value,
    IN      ULONG           Base OPTIONAL,
    IN OUT  PUNICODE_STRING String
);



NATIVE_API
NTSTATUS
NTAPI
RtlGUIDFromString(
    IN  PUNICODE_STRING GuidString,
    OUT REFGUID Guid
);

#define HASH_STRING_ALGORITHM_DEFAULT   (0u)
#define HASH_STRING_ALGORITHM_X65599    (1u)
#define HASH_STRING_ALGORITHM_INVALID   (~0ull)

NATIVE_API
NTSTATUS
NTAPI
RtlHashUnicodeString(
    IN  PCUNICODE_STRING    String,
    IN  BOOLEAN             CaseInSensitive,
    IN  ULONG               HashAlgorithm,
    OUT PULONG              HashValue
);

NATIVE_API
VOID
NTAPI
RtlInitAnsiString(
    IN OUT  PANSI_STRING    DestinationString,
    IN      LPCSTR          SourceString
);

NATIVE_API
NTSTATUS
NTAPI
RtlInitAnsiStringEx(
    IN OUT  PANSI_STRING    DestinationString,
    IN      LPCSTR          SourceString
);

NATIVE_API
VOID
NTAPI
RtlInitUnicodeString(
    IN OUT  PUNICODE_STRING DestinationString,
    IN      LPCWSTR         SourceString
);

NATIVE_API
NTSTATUS
NTAPI
RtlInitUnicodeStringEx(
    IN OUT  PUNICODE_STRING DestinationString,
    IN      LPCWSTR         SourceString
);

NATIVE_API
NTSTATUS
NTAPI
RtlAnsiStringToUnicodeString(
    IN OUT  PUNICODE_STRING DestinationString,
    IN      PANSI_STRING    SourceString,
    IN      BOOLEAN         AllocateDestinationString
);

NATIVE_API
NTSTATUS
NTAPI
RtlUnicodeStringToAnsiString(
    IN OUT  PANSI_STRING    DestinationString,
    IN      PUNICODE_STRING SourceString,
    IN      BOOLEAN         AllocateDestinationString
);

NATIVE_API
VOID
NTAPI
RtlRunEncodeUnicodeString(
    IN OUT  PUCHAR          Seed,
    IN      PUNICODE_STRING String
);

NATIVE_API
VOID
NTAPI
RtlRunDecodeUnicodeString(
    IN UCHAR            Seed,
    IN PUNICODE_STRING  String
);


NATIVE_API ULONG   NlsAnsiCodePage;
NATIVE_API BOOLEAN NlsMbCodePageTag;
NATIVE_API BOOLEAN NlsMbOemCodePageTag;

FORCEINLINE
VOID
RtlInitEmptyAnsiString(
    OUT PANSI_STRING    AnsiString,
    IN  PCSTR           Buffer,
    IN  ULONG_PTR       BufferSize
)
{
    AnsiString->Length        = 0;
    AnsiString->MaximumLength = (USHORT)BufferSize;
    AnsiString->Buffer        = (PSTR)Buffer;
}

FORCEINLINE
VOID
RtlInitEmptyUnicodeString(
    OUT PUNICODE_STRING UnicodeString,
    IN  PCWSTR          Buffer,
    IN  ULONG_PTR       BufferSize
)
{
    UnicodeString->Length        = 0;
    UnicodeString->MaximumLength = (USHORT)BufferSize;
    UnicodeString->Buffer        = (PWSTR)Buffer;
}


#endif // _NTIFS_

#if CPP_DEFINED

EXTCPP
template<typename StringType>
inline
VOID
RtlInitEmptyString(
    OUT StringType*     String,
    IN  PCWSTR          Buffer = nullptr,
    IN  ULONG_PTR       BufferSize = 0
)
{
    String->Length        = 0;
    String->MaximumLength = (USHORT)BufferSize;
    String->Buffer        = (PWSTR)Buffer;
}

#endif // cpp

inline
NTSTATUS
RtlSetUnicodeString(
    PUNICODE_STRING UnicodeString,
    PCWSTR          String,
    ULONG_PTR       DEF_VAL(Length, -1)
)
{
    if (UnicodeString == nullptr)
        return STATUS_INVALID_PARAMETER;

    if (String == nullptr)
    {
        RtlFreeUnicodeString(UnicodeString);
        return STATUS_SUCCESS;
    }

    if (UnicodeString->Buffer == nullptr)
    {
        return RtlCreateUnicodeString(UnicodeString, String) ? STATUS_SUCCESS : STATUS_NO_MEMORY;
    }

    if (String == nullptr)
    {
        UnicodeString->Buffer[0]    = 0;
        UnicodeString->Length       = 0;

        return STATUS_SUCCESS;
    }

    if (Length == -1)
        Length = StrLengthW(String) * sizeof(WCHAR);

    if (Length < UnicodeString->MaximumLength)
    {
        UnicodeString->Length = (USHORT)Length;
        CopyMemory(UnicodeString->Buffer, String, Length);
        UnicodeString->Buffer[Length / sizeof(WCHAR)] = 0;

        return STATUS_SUCCESS;
    }

    RtlFreeUnicodeString(UnicodeString);
    return RtlCreateUnicodeString(UnicodeString, String) ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}

#endif // _NTSTRING_H_9bfab99e_9722_48ad_9a23_53edcb64be76_
