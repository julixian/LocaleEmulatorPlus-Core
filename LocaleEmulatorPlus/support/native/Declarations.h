#ifndef _NATIVEAPI_H_536f1070_45de_44d7_9e53_6c5db2ff00d3
#define _NATIVEAPI_H_536f1070_45de_44d7_9e53_6c5db2ff00d3

#pragma warning(disable:4103)

#include "Types.h"

_ML_C_HEAD_

#include "Debug.h"
#include "Exception.h"
#include "File.h"
#include "Lpc.h"
#include "Memory.h"
#include "Process.h"
#ifndef _NTMODULE_H_da9e04cd_9069_411f_8914_6b5fce052ca8_
#define _NTMODULE_H_da9e04cd_9069_411f_8914_6b5fce052ca8_



#endif // _NTMODULE_H_da9e04cd_9069_411f_8914_6b5fce052ca8_
#include "Registry.h"
#include "Loader.h"
#include "String.h"
#include "Synchronization.h"
#include "Timer.h"
#include "Token.h"
#include "User.h"
#include "Runtime.h"
#include "Nls.h"
#ifndef _NTGDI_H_5cbb7a71_7f51_4a8a_bb90_038b762c3ed7_
#define _NTGDI_H_5cbb7a71_7f51_4a8a_bb90_038b762c3ed7_


_ML_C_HEAD_



inline
ULONG
NTAPI
NtGdiGetGlyphOutline(
    IN  HDC             hDc,
    IN  WCHAR           UnicodeChar,
    IN  ULONG           Format,
    OUT LPGLYPHMETRICS  GlyphMetrics,
    IN  ULONG           BufferSize,
    OUT PVOID           Buffer,
    IN  LPMAT2          Mat2,
    IN  BOOL            IgnoreRotation
)
{
    return 0;
}

inline
HFONT
NTAPI
NtGdiHfontCreate(
    IN  PENUMLOGFONTEXDVW   EnumLogFont,
    IN  ULONG               SizeOfEnumLogFont,
    IN  LONG                LogFontType,
    IN  LONG                Unknown,
    IN  PVOID               FreeListLocalFont
)
{
    return 0;
}



typedef struct
{
    ULONG SizeOfSelf;
    ULONG Unknown1;
    ULONG Unknown2;
    ULONG FontHandle;
    ULONG FontCount;
    ULONG Unknown5;

} FONT_REALIZATION_INFO, *PFONT_REALIZATION_INFO;

NATIVE_API
BOOL
NTAPI
GetFontRealizationInfo(
    HDC DC,
    PFONT_REALIZATION_INFO RealizationInfo
);

typedef struct
{
    ULONG Reserved[4];
    WCHAR FontFile[MAX_NTPATH];

} FONT_FILE_INFO, *PFONT_FILE_INFO;

NATIVE_API
BOOL
NTAPI
GetFontFileInfo(
    ULONG           FontHandle,
    ULONG           FontIndex,
    PFONT_FILE_INFO FontFileInfo,
    ULONG           InfoLength,
    PULONG          ReturnedLength
);


_ML_C_TAIL_

#endif // _NTGDI_H_5cbb7a71_7f51_4a8a_bb90_038b762c3ed7_
#ifndef _NTPO_H_87cbac49_8d82_445e_8ac6_19ad5037eeb4_
#define _NTPO_H_87cbac49_8d82_445e_8ac6_19ad5037eeb4_



NTSTATUS
NTAPI
NtSetThreadExecutionState(
    IN  EXECUTION_STATE     Flags,
    OUT EXECUTION_STATE*    PreviousFlags
);

#endif // _NTPO_H_87cbac49_8d82_445e_8ac6_19ad5037eeb4_
// Font Management Services

#include "Font.h"

_ML_C_TAIL_


#endif // _NATIVEAPI_H_536f1070_45de_44d7_9e53_6c5db2ff00d3
