#include "stdafx.h"

ULONG (NTAPI *GdiGetCodePage)(HDC NewDC);

static BOOL IsStockFontObjectIndex(LONG Object)
{
    const ULONG_PTR *Index;
    static const ULONG_PTR StockObjectIndex[] =
    {
        ANSI_FIXED_FONT,
        ANSI_VAR_FONT,
        DEVICE_DEFAULT_FONT,
        DEFAULT_GUI_FONT,
        OEM_FIXED_FONT,
        SYSTEM_FONT,
        SYSTEM_FIXED_FONT,
    };

    FOR_EACH_ARRAY(Index, StockObjectIndex)
    {
        if (Object == *Index)
            return TRUE;
    }

    return FALSE;
}

HFONT GetFontFromFont(PLepGlobalData GlobalData, HFONT Font)
{
    LOGFONTW LogFont;

    if (GetObjectW(Font, sizeof(LogFont), &LogFont) == 0)
        return nullptr;

    LogFont.lfCharSet = GlobalData->GetLepb()->DefaultCharset;
    Font = CreateFontIndirectW(&LogFont);

    return Font;
}

HFONT GetFontFromDC(PLepGlobalData GlobalData, HDC hDC)
{
    HFONT       Font;
    LOGFONTW    LogFont;

    Font = (HFONT)GetCurrentObject(hDC, OBJ_FONT);
    if (Font == nullptr)
        return nullptr;

    return GetFontFromFont(GlobalData, Font);
}

BOOL IsGdiHookBypassed()
{
    return FindThreadFrame(GDI_HOOK_BYPASS) != nullptr;
}

HFONT CreateFontIndirectBypassA(CONST LOGFONTA *lplf)
{
    TEB_ACTIVE_FRAME Bypass(GDI_HOOK_BYPASS);
    Bypass.Push();

    return CreateFontIndirectA(lplf);
}

HFONT CreateFontIndirectBypassW(CONST LOGFONTW *lplf)
{
    TEB_ACTIVE_FRAME Bypass(GDI_HOOK_BYPASS);
    Bypass.Push();

    return CreateFontIndirectW(lplf);
}

VOID LepGlobalData::GetTextMetricsAFromLogFont(PTEXTMETRICA TextMetricA, CONST LOGFONTW *LogFont)
{
    HDC     hDC;
    HFONT   Font, OldFont;
    ULONG   GraphicsMode;

    hDC = this->CreateCompatibleDC(nullptr);
    if (hDC == nullptr)
        return;

    GraphicsMode = SetGraphicsMode(hDC, GM_ADVANCED);

    LOOP_ONCE
    {
        Font = CreateFontIndirectBypassW(LogFont);
        if (Font == nullptr)
            break;

        OldFont = (HFONT)SelectObject(hDC, Font);
        if (OldFont != nullptr)
        {
            TextMetricA->tmCharSet = 0x80;
            GetTextMetricsA(hDC, TextMetricA);
        }

        SelectObject(hDC, OldFont);
        DeleteObject(Font);
    }

    DeleteDC(hDC);
}

VOID LepGlobalData::GetTextMetricsWFromLogFont(PTEXTMETRICW TextMetricW, CONST LOGFONTW *LogFont)
{
    HDC     hDC;
    HFONT   Font, OldFont;
    ULONG   GraphicsMode;

    hDC = this->CreateCompatibleDC(nullptr);
    if (hDC == nullptr)
        return;

    GraphicsMode = SetGraphicsMode(hDC, GM_ADVANCED);

    LOOP_ONCE
    {
        Font = CreateFontIndirectBypassW(LogFont);
        if (Font == nullptr)
            break;

        OldFont = (HFONT)SelectObject(hDC, Font);
        if (OldFont != nullptr)
            GetTextMetricsW(hDC, TextMetricW);

        SelectObject(hDC, OldFont);
        DeleteObject(Font);
    }

    DeleteDC(hDC);
}

static VOID CopyUnicodeStringToFixedBuffer(PWSTR Buffer, ULONG_PTR Count, PCUNICODE_STRING String)
{
    ULONG_PTR BytesToCopy;

    if (Count == 0)
        return;

    BytesToCopy = ML_MIN((Count - 1) * sizeof(WCHAR), String->Length);
    BytesToCopy &= ~(sizeof(WCHAR) - 1);

    CopyMemory(Buffer, String->Buffer, BytesToCopy);
    Buffer[BytesToCopy / sizeof(WCHAR)] = 0;
}

static VOID BuildTextMetricCacheKey(PWSTR Buffer, ULONG_PTR Count, LPENUMLOGFONTEXW LogFont)
{
    PWSTR Cursor, End;
    ULONG Charset;
    WCHAR Digits[16];
    ULONG_PTR DigitCount;

    if (Count == 0)
        return;

    Cursor = Buffer;
    End = Buffer + Count - 1;

    for (PCWSTR Source = LogFont->elfFullName; *Source != 0 && Cursor < End; ++Source)
        *Cursor++ = CHAR_UPPER(*Source);

    if (Cursor < End)
        *Cursor++ = L'@';

    Charset = LogFont->elfLogFont.lfCharSet;
    DigitCount = 0;
    do
    {
        Digits[DigitCount++] = (WCHAR)(L'0' + Charset % 10);
        Charset /= 10;
    } while (Charset != 0 && DigitCount != countof(Digits));

    while (DigitCount != 0 && Cursor < End)
        *Cursor++ = Digits[--DigitCount];

    *Cursor = 0;
}

PTEXT_METRIC_INTERNAL LepGlobalData::GetTextMetricFromCache(LPENUMLOGFONTEXW LogFont)
{
    WCHAR buf[LF_FULLFACESIZE * 2];

    BuildTextMetricCacheKey(buf, countof(buf), LogFont);

    return this->TextMetricCache.Get((PWSTR)buf);
}

VOID LepGlobalData::AddTextMetricToCache(LPENUMLOGFONTEXW LogFont, PTEXT_METRIC_INTERNAL TextMetric)
{
    WCHAR buf[LF_FULLFACESIZE * 2];

    BuildTextMetricCacheKey(buf, countof(buf), LogFont);

    this->TextMetricCache.Add((PWSTR)buf, *TextMetric);
}

HGDIOBJ NTAPI LepGetStockObject(LONG Object)
{
    PLepGlobalData   GlobalData = LepGetGlobalData();

    /*if (!GlobalData->HookRoutineData.Gdi32.StockObjectInitialized)
    {
        PROTECT_SECTION(&GlobalData->HookRoutineData.Gdi32.GdiLock)
        {
            if (GlobalData->HookRoutineData.Gdi32.StockObjectInitialized)
                break;

            FOR_EACH(Index, StockObjectIndex, countof(StockObjectIndex))
            {
                GlobalData->HookRoutineData.Gdi32.StockObject[*Index] = GetFontFromFont(GlobalData, (HFONT)GlobalData->GetStockObject(*Index));
            }

            GlobalData->HookRoutineData.Gdi32.StockObjectInitialized = TRUE;
        }
    }*/

    LOOP_ONCE
    {
        if (Object >= countof(GlobalData->HookRoutineData.Gdi32.StockObject))
            break;

        auto& StockObject = GlobalData->HookRoutineData.Gdi32.StockObject[Object];

        if (StockObject != nullptr)
            return StockObject;

        if (IsStockFontObjectIndex(Object)) {
            PROTECT_SECTION(&GlobalData->HookRoutineData.Gdi32.GdiLock) {
                if (StockObject == nullptr)
                    return StockObject = GetFontFromFont(GlobalData, (HFONT)GlobalData->GetStockObject(Object));
                return StockObject;
            }
        }
    }

    return GlobalData->GetStockObject(Object);
}

BOOL NTAPI LepDeleteObject(HGDIOBJ GdiObject)
{
    HGDIOBJ*        StockObject;
    PLepGlobalData   GlobalData = LepGetGlobalData();

    //if (GdiObject == nullptr || GlobalData->HookRoutineData.Gdi32.StockObjectInitialized == FALSE)
    if (GdiObject == nullptr)
        return TRUE;

    FOR_EACH_ARRAY(StockObject, GlobalData->HookRoutineData.Gdi32.StockObject)
    {
        if (GdiObject == *StockObject)
            return TRUE;
    }

    return GlobalData->DeleteObject(GdiObject);
}

HDC NTAPI LepCreateCompatibleDC(HDC hDC)
{
    HDC             NewDC;
    HFONT           Font;
    LOGFONTW        LogFont;
    PLepGlobalData   GlobalData = LepGetGlobalData();

    NewDC = GlobalData->CreateCompatibleDC(hDC);

    if (NewDC == nullptr)
        return NewDC;

    Font = (HFONT)GetCurrentObject(NewDC, OBJ_FONT);
    if (Font == nullptr)
        return NewDC;

    SelectObject(NewDC, GetStockObject(SYSTEM_FONT));

    return NewDC;
}

ForceInline WCHAR LepUpcaseAsciiW(WCHAR Character)
{
    return Character >= L'a' && Character <= L'z' ? Character - (L'a' - L'A') : Character;
}

ForceInline BOOL LepEqualStringInsensitiveW(PCWSTR Left, PCWSTR Right)
{
    for (;;)
    {
        WCHAR LeftCharacter = LepUpcaseAsciiW(*Left++);
        WCHAR RightCharacter = LepUpcaseAsciiW(*Right++);

        if (LeftCharacter != RightCharacter)
            return FALSE;

        if (LeftCharacter == 0)
            return TRUE;
    }
}

NTSTATUS
LepGlobalData::
GetNameRecordFromNameTable(
    PVOID           TableBuffer,
    ULONG_PTR       TableSize,
    ULONG_PTR       NameID,
    ULONG_PTR       LanguageID,
    PUNICODE_STRING Name
)
{
    using namespace Gdi;

    ULONG_PTR               StorageOffset, NameRecordCount;
    PTT_NAME_TABLE_HEADER   NameHeader;
    PTT_NAME_RECORD         NameRecord, NameRecordUser, NameRecordEn;

    NameHeader = (PTT_NAME_TABLE_HEADER)TableBuffer;

    NameRecordCount = Bswap(NameHeader->NameRecordCount);
    StorageOffset = Bswap(NameHeader->StorageOffset);

    if (StorageOffset >= TableSize)
        return STATUS_NOT_SUPPORTED;

    if (StorageOffset < NameRecordCount * sizeof(*NameRecord))
        return STATUS_NOT_SUPPORTED;

    LanguageID      = Bswap((USHORT)LanguageID);
    NameRecordUser  = nullptr;
    NameRecordEn    = nullptr;
    NameRecord      = (PTT_NAME_RECORD)(NameHeader + 1);

    FOR_EACH(NameRecord, NameRecord, NameRecordCount)
    {
        if (NameRecord->PlatformID != TT_PLATFORM_ID_WINDOWS)
            continue;

        if (NameRecord->EncodingID != TT_ENCODEING_ID_UTF16_BE)
            continue;

        if (NameRecord->NameID != NameID)
            continue;

        if (NameRecord->LanguageID == Bswap((USHORT)0x0409))
            NameRecordEn = NameRecord;

        if (NameRecord->LanguageID != LanguageID)
            continue;

        NameRecordUser = NameRecord;
        break;
    }

    NameRecordUser = NameRecordUser == nullptr ? NameRecordEn : NameRecordUser;
    if (NameRecordUser == nullptr)
        return STATUS_NOT_FOUND;

    PWSTR       FaceName, Buffer;
    ULONG_PTR   Offset, Length;

    Offset = StorageOffset + Bswap(NameRecordUser->StringOffset);
    Length = Bswap(NameRecordUser->StringLength);
    FaceName = (PWSTR)PtrAdd(TableBuffer, Offset);

    Buffer = Name->Buffer;
    Length = (USHORT)ML_MIN(Length, Name->MaximumLength);
    Name->Length = Length;

    for (ULONG_PTR Index = 0; Index != Length / sizeof(WCHAR); ++Index)
    {
        Buffer[Index] = Bswap(FaceName[Index]);
    }

    if (Length < Name->MaximumLength)
        Buffer[Length / sizeof(WCHAR)] = 0;

    return STATUS_SUCCESS;
}

NTSTATUS LepGlobalData::AdjustFontDataInternal(PADJUST_FONT_DATA AdjustData)
{
    NTSTATUS        Status;
    PVOID           Table;
    ULONG_PTR       TableSize, TableName;
    WCHAR           FaceNameBuffer[LF_FACESIZE];
    WCHAR           FullNameBuffer[LF_FULLFACESIZE];
    UNICODE_STRING  FaceName, FullName;

    if (FLAG_ON(AdjustData->FontType, RASTER_FONTTYPE))
        return STATUS_NOT_SUPPORTED;

    TableName = Gdi::TT_TABLE_TAG_NAME;
    TableSize = GetFontData(AdjustData->DC, TableName, 0, 0, 0);
    if (TableSize == GDI_ERROR)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    Table = AllocStack(TableSize);
    TableSize = GetFontData(AdjustData->DC, TableName, 0, Table, TableSize);
    if (TableSize == GDI_ERROR)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    RtlInitEmptyString(&FaceName, FaceNameBuffer, sizeof(FaceNameBuffer));
    RtlInitEmptyString(&FullName, FullNameBuffer, sizeof(FullNameBuffer));

    Status = this->GetNameRecordFromNameTable(
                Table,
                TableSize,
                Gdi::TT_NAME_ID_FACENAME,
                this->GetLepPeb()->OriginalLocaleID,
                &FaceName
            );

    PCWSTR lfFaceName = AdjustData->EnumLogFontEx->elfLogFont.lfFaceName;
    BOOL Vertical = lfFaceName[0] == '@';

    if (NT_FAILED(Status) || !LepEqualStringInsensitiveW(FaceName.Buffer, lfFaceName + Vertical))
        return STATUS_CONTEXT_MISMATCH;

    Status = this->GetNameRecordFromNameTable(
                Table,
                TableSize,
                Gdi::TT_NAME_ID_FACENAME,
                this->GetLepb()->LocaleID,
                &FaceName
            );

    Status = NT_SUCCESS(Status) ?
                this->GetNameRecordFromNameTable(
                    Table,
                    TableSize,
                    Gdi::TT_NAME_ID_FULLNAME,
                    this->GetLepb()->LocaleID,
                    &FullName)
                : Status;

    if (NT_SUCCESS(Status))
    {
        BOOL        Vertical;

        Vertical = AdjustData->EnumLogFontEx->elfLogFont.lfFaceName[0] == '@';

        CopyUnicodeStringToFixedBuffer(
            AdjustData->EnumLogFontEx->elfLogFont.lfFaceName + Vertical,
            countof(AdjustData->EnumLogFontEx->elfLogFont.lfFaceName) - Vertical,
            &FaceName
        );

        CopyUnicodeStringToFixedBuffer(
            AdjustData->EnumLogFontEx->elfFullName + Vertical,
            countof(AdjustData->EnumLogFontEx->elfFullName) - Vertical,
            &FullName
        );
    }

    return Status;
}

NTSTATUS LepGlobalData::AdjustFontData(HDC DC, LPENUMLOGFONTEXW EnumLogFontEx, PTEXT_METRIC_INTERNAL TextMetric, ULONG_PTR FontType)
{
    NTSTATUS Status;
    ADJUST_FONT_DATA AdjustData;

    ZeroMemory(&AdjustData, sizeof(AdjustData));
    AdjustData.EnumLogFontEx = EnumLogFontEx;
    AdjustData.DC = DC;

    Status = STATUS_UNSUCCESSFUL;

    LOOP_ONCE
    {
        AdjustData.Font = CreateFontIndirectBypassW(&EnumLogFontEx->elfLogFont);
        if (AdjustData.Font == nullptr)
            break;

        AdjustData.OldFont = (HFONT)SelectObject(DC, AdjustData.Font);
        if (AdjustData.OldFont == nullptr)
            break;

        Status = this->AdjustFontDataInternal(&AdjustData);
        if (Status == STATUS_CONTEXT_MISMATCH)
            break;

        if (TextMetric != nullptr)
        {
            if (GetTextMetricsA(DC, &TextMetric->TextMetricA) &&
                GetTextMetricsW(DC, &TextMetric->TextMetricW))
            {
                TextMetric->Filled = TRUE;
            }
        }
    }

    if (AdjustData.OldFont != nullptr)
        SelectObject(DC, AdjustData.OldFont);

    if (AdjustData.Font != nullptr)
        this->DeleteObject(AdjustData.Font);

    return Status;
}

VOID ConvertAnsiLogfontToUnicode(PLOGFONTW LogFontW, PLOGFONTA LogFontA)
{
    CopyMemory(LogFontW, LogFontA, PtrOffset(&LogFontA->lfFaceName, LogFontA));
    RtlMultiByteToUnicodeN(LogFontW->lfFaceName, sizeof(LogFontW->lfFaceName), nullptr, LogFontA->lfFaceName, StrLengthA(LogFontA->lfFaceName) + 1);
}

VOID ConvertUnicodeLogfontToAnsi(PLOGFONTA LogFontA, PLOGFONTW LogFontW)
{
    CopyMemory(LogFontA, LogFontW, PtrOffset(&LogFontW->lfFaceName, LogFontW));
    RtlUnicodeToMultiByteN(LogFontA->lfFaceName, sizeof(LogFontA->lfFaceName), nullptr, LogFontW->lfFaceName, (StrLengthW(LogFontW->lfFaceName) + 1) * sizeof(WCHAR));
}

VOID ConvertUnicodeTextMetricToAnsi(PTEXTMETRICA TextMetricA, CONST TEXTMETRICW *TextMetricW)
{
    TextMetricA->tmHeight           = TextMetricW->tmHeight;
    TextMetricA->tmAscent           = TextMetricW->tmAscent;
    TextMetricA->tmDescent          = TextMetricW->tmDescent;
    TextMetricA->tmInternalLeading  = TextMetricW->tmInternalLeading;
    TextMetricA->tmExternalLeading  = TextMetricW->tmExternalLeading;
    TextMetricA->tmAveCharWidth     = TextMetricW->tmAveCharWidth;
    TextMetricA->tmMaxCharWidth     = TextMetricW->tmMaxCharWidth;
    TextMetricA->tmWeight           = TextMetricW->tmWeight;
    TextMetricA->tmOverhang         = TextMetricW->tmOverhang;
    TextMetricA->tmDigitizedAspectX = TextMetricW->tmDigitizedAspectX;
    TextMetricA->tmDigitizedAspectY = TextMetricW->tmDigitizedAspectY;

    TextMetricA->tmFirstChar        = TextMetricW->tmStruckOut;
    TextMetricA->tmLastChar         = ML_MIN(0xFF, TextMetricW->tmLastChar);
    TextMetricA->tmDefaultChar      = TextMetricW->tmDefaultChar;
    TextMetricA->tmBreakChar        = TextMetricW->tmBreakChar;

    TextMetricA->tmItalic           = TextMetricW->tmItalic;
    TextMetricA->tmUnderlined       = TextMetricW->tmUnderlined;
    TextMetricA->tmStruckOut        = TextMetricW->tmStruckOut;
    TextMetricA->tmPitchAndFamily   = TextMetricW->tmPitchAndFamily;
    TextMetricA->tmCharSet          = TextMetricW->tmCharSet;
}

static CONST TEXTMETRICW *PrepareEnumFontCallbackMetricW(
    NEWTEXTMETRICEXW *CallbackMetric,
    CONST TEXTMETRICW *OriginalMetric,
    CONST TEXTMETRICW *AdjustedMetric,
    DWORD FontType
)
{
    if (!FLAG_ON(FontType, TRUETYPE_FONTTYPE))
        return AdjustedMetric;

    CopyMemory(CallbackMetric, OriginalMetric, sizeof(*CallbackMetric));
    CopyMemory(&CallbackMetric->ntmTm, AdjustedMetric, sizeof(*AdjustedMetric));

    return (CONST TEXTMETRICW *)&CallbackMetric->ntmTm;
}

INT NTAPI LepEnumFontCallbackAFromW(CONST LOGFONTW *lf, CONST TEXTMETRICW *TextMetricW, DWORD FontType, LPARAM param)
{
    ENUMLOGFONTEXA          EnumLogFontA;
    LPENUMLOGFONTEXW        EnumLogFontW;
    PGDI_ENUM_FONT_PARAM    EnumParam;
    PTEXT_METRIC_INTERNAL   TextMetric;
    TEXTMETRICA             TextMetricA;
    PTEXTMETRICA            tma;

    TextMetric = FIELD_BASE(TextMetricW, TEXT_METRIC_INTERNAL, TextMetricW);
    EnumParam = (PGDI_ENUM_FONT_PARAM)param;

    EnumLogFontW = (LPENUMLOGFONTEXW)lf;
    ConvertUnicodeLogfontToAnsi(&EnumLogFontA.elfLogFont, &EnumLogFontW->elfLogFont);

    RtlUnicodeToMultiByteN((PSTR)EnumLogFontA.elfFullName, sizeof(EnumLogFontA.elfFullName), nullptr, EnumLogFontW->elfFullName, (StrLengthW(EnumLogFontW->elfFullName) + 1) * sizeof(WCHAR));
    RtlUnicodeToMultiByteN((PSTR)EnumLogFontA.elfScript, sizeof(EnumLogFontA.elfScript), nullptr, EnumLogFontW->elfScript, (StrLengthW(EnumLogFontW->elfScript) + 1) * sizeof(WCHAR));
    RtlUnicodeToMultiByteN((PSTR)EnumLogFontA.elfStyle, sizeof(EnumLogFontA.elfStyle), nullptr, EnumLogFontW->elfStyle, (StrLengthW(EnumLogFontW->elfStyle) + 1) * sizeof(WCHAR));

    if (TextMetric->VerifyMagic() == FALSE)
    {
        ConvertUnicodeTextMetricToAnsi(&TextMetricA, TextMetricW);
        tma = &TextMetricA;
    }
    else
    {
        tma = &TextMetric->TextMetricA;
    }

    return ((FONTENUMPROCA)(EnumParam->Callback))(&EnumLogFontA.elfLogFont, tma, FontType, EnumParam->lParam);
}

INT NTAPI LepEnumFontCallbackW(CONST LOGFONTW *lf, CONST TEXTMETRICW *TextMetricW, DWORD FontType, LPARAM param)
{
#if ML_AMD64
    NTSTATUS                Status;
    PGDI_ENUM_FONT_PARAM    EnumParam;
    TEXT_METRIC_INTERNAL    TextMetric;
    ENUMLOGFONTEXW          EnumLogFontEx;
    NEWTEXTMETRICEXW        CallbackMetric;
    CONST TEXTMETRICW      *CallbackTextMetricW;
    ULONG_PTR               LogFontCharset;

    EnumParam = (PGDI_ENUM_FONT_PARAM)param;
    EnumLogFontEx = *(LPENUMLOGFONTEXW)lf;
    LogFontCharset = EnumLogFontEx.elfLogFont.lfCharSet;

    LOOP_ONCE
    {
        if (EnumParam->Charset == DEFAULT_CHARSET &&
            (lf->lfCharSet == ANSI_CHARSET ||
             lf->lfCharSet == DEFAULT_CHARSET ||
             lf->lfCharSet == EnumParam->GlobalData->GetLepPeb()->OriginalCharset))
        {
            EnumLogFontEx.elfLogFont.lfCharSet = EnumParam->GlobalData->GetLepb()->DefaultCharset;
        }

        Status = EnumParam->GlobalData->AdjustFontData(EnumParam->DC, &EnumLogFontEx, &TextMetric, FontType);

        ENUMLOGFONTEXW Captured = EnumLogFontEx;

        Captured.elfLogFont.lfCharSet = LogFontCharset;

        if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_CONTEXT_MISMATCH)
        {
            TextMetric.Magic = 0;
            EnumParam->GlobalData->AddTextMetricToCache(&Captured, &TextMetric);
            return TRUE;
        }

        EnumParam->GlobalData->AddTextMetricToCache(&Captured, &TextMetric);
    }

    if (TextMetric.Filled == FALSE)
    {
        TextMetric.TextMetricW = *TextMetricW;
        TextMetric.Magic = 0;
        EnumLogFontEx.elfLogFont.lfCharSet = LogFontCharset;
    }

    CallbackTextMetricW = PrepareEnumFontCallbackMetricW(&CallbackMetric, TextMetricW, &TextMetric.TextMetricW, FontType);

    return ((FONTENUMPROCW)(EnumParam->Callback))(&EnumLogFontEx.elfLogFont, CallbackTextMetricW, FontType, EnumParam->lParam);
#else
    NTSTATUS                Status;
    PGDI_ENUM_FONT_PARAM    EnumParam;
    TEXT_METRIC_INTERNAL    TextMetric;
    ENUMLOGFONTEXW          EnumLogFontEx;
    LPENUMLOGFONTEXW        OriginalLogFontEx;
    ULONG_PTR               LogFontCharset;

    EnumParam = (PGDI_ENUM_FONT_PARAM)param;
    OriginalLogFontEx = (LPENUMLOGFONTEXW)lf;
    EnumLogFontEx = *OriginalLogFontEx;
    LogFontCharset = EnumLogFontEx.elfLogFont.lfCharSet;

    LOOP_ONCE
    {
        PTEXT_METRIC_INTERNAL CachedTextMetric;

        CachedTextMetric = EnumParam->GlobalData->GetTextMetricFromCache(&EnumLogFontEx);
        if (CachedTextMetric != nullptr)
        {
            if (CachedTextMetric->VerifyMagic() == FALSE)
                return TRUE;

            if (CachedTextMetric->Filled == FALSE)
                break;

            TextMetric.TextMetricA = CachedTextMetric->TextMetricA;
            TextMetric.TextMetricW = CachedTextMetric->TextMetricW;
            TextMetric.Filled = TRUE;
            break;
        }

        LOOP_ONCE
        {
            if (EnumParam->Charset != DEFAULT_CHARSET)
                break;

            if (lf->lfCharSet != ANSI_CHARSET &&
                lf->lfCharSet != DEFAULT_CHARSET &&
                lf->lfCharSet != EnumParam->GlobalData->GetLepPeb()->OriginalCharset)
            {
                break;
            }

            EnumLogFontEx.elfLogFont.lfCharSet = EnumParam->GlobalData->GetLepb()->DefaultCharset;
        }

        //PrintConsole(L"Original:        %s\n", lf->lfFaceName);

        Status = EnumParam->GlobalData->AdjustFontData(EnumParam->DC, &EnumLogFontEx, &TextMetric, FontType);

        ENUMLOGFONTEXW Captured = EnumLogFontEx;

        Captured.elfLogFont.lfCharSet = LogFontCharset;

        if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_CONTEXT_MISMATCH)
        {
            TextMetric.Magic = 0;
            EnumParam->GlobalData->AddTextMetricToCache(&Captured, &TextMetric);
            return TRUE;
        }

        EnumParam->GlobalData->AddTextMetricToCache(&Captured, &TextMetric);
    }

    if (TextMetric.Filled == FALSE)
    {
        TextMetric.TextMetricW = *TextMetricW;
        TextMetric.Magic = 0;
        EnumLogFontEx.elfLogFont.lfCharSet = LogFontCharset;
    }

    TextMetricW = &TextMetric.TextMetricW;

    return ((FONTENUMPROCW)(EnumParam->Callback))(&EnumLogFontEx.elfLogFont, TextMetricW, FontType, EnumParam->lParam);
#endif
}

int NTAPI LepEnumFontFamiliesExW(HDC hdc, LPLOGFONTW lpLogfont, FONTENUMPROCW lpProc, LPARAM lParam, DWORD dwFlags)
{
    INT                 Result;
    GDI_ENUM_FONT_PARAM Param;
    LOGFONTW            lf;
    PLepGlobalData       GlobalData = LepGetGlobalData();

#if ML_AMD64
    if (NT_FAILED(Param.Prepare(GlobalData)))
        return FALSE;

    Param.Callback      = lpProc;
    Param.GlobalData    = GlobalData;
    Param.lParam        = lParam;
    Param.Charset       = lpLogfont == nullptr ? DEFAULT_CHARSET : lpLogfont->lfCharSet;

    return GlobalData->EnumFontFamiliesExW(hdc, lpLogfont, LepEnumFontCallbackW, (LPARAM)&Param, dwFlags);
#else
    if (NT_FAILED(Param.Prepare(GlobalData)))
        return FALSE;

    Param.Callback      = lpProc;
    Param.GlobalData    = GlobalData;
    Param.lParam        = lParam;
    Param.Charset       = lpLogfont == nullptr ? DEFAULT_CHARSET : lpLogfont->lfCharSet;

	if (lpLogfont == nullptr)
		ZeroMemory(&lf, sizeof(lf));
	else
		lf = *lpLogfont;

    return GlobalData->EnumFontFamiliesExW(hdc, &lf, LepEnumFontCallbackW, (LPARAM)&Param, dwFlags);
#endif
}

int NTAPI LepEnumFontFamiliesExA(HDC hdc, LPLOGFONTA lpLogfont, FONTENUMPROCA lpProc, LPARAM lParam, DWORD dwFlags)
{
    LOGFONTW            lf;
    PLepGlobalData       GlobalData = LepGetGlobalData();
    GDI_ENUM_FONT_PARAM Param;

    Param.Callback      = lpProc;
    Param.GlobalData    = GlobalData;
    Param.lParam        = lParam;
	Param.Charset       = lpLogfont == nullptr ? DEFAULT_CHARSET : lpLogfont->lfCharSet;

	if (lpLogfont == nullptr)
		ZeroMemory(&lf, sizeof(lf));
	else
		ConvertAnsiLogfontToUnicode(&lf, lpLogfont);

    return LepEnumFontFamiliesExW(hdc, &lf, LepEnumFontCallbackAFromW, (LPARAM)&Param, dwFlags);
}

int NTAPI LepEnumFontFamiliesW(HDC hdc, PCWSTR lpszFamily, FONTENUMPROCW lpProc, LPARAM lParam)
{
    GDI_ENUM_FONT_PARAM Param;
    LOGFONTW            LocalLogFont;
    PLepGlobalData       GlobalData = LepGetGlobalData();

    if (NT_FAILED(Param.Prepare(GlobalData)))
        return FALSE;

    Param.Callback      = lpProc;
    Param.GlobalData    = GlobalData;
    Param.lParam        = lParam;
    Param.Charset       = DEFAULT_CHARSET;

    return GlobalData->EnumFontFamiliesW(hdc, lpszFamily, LepEnumFontCallbackW, (LPARAM)&Param);
}

int NTAPI LepEnumFontFamiliesA(HDC hdc, PCSTR lpszFamily, FONTENUMPROCA lpProc, LPARAM lParam)
{
    INT                 Result;
    PWSTR               Family;
    GDI_ENUM_FONT_PARAM Param;
    LOGFONTW            LocalLogFont;
    PLepGlobalData       GlobalData = LepGetGlobalData();

    Param.Callback      = lpProc;
    Param.GlobalData    = GlobalData;
    Param.lParam        = lParam;
    Param.Charset       = DEFAULT_CHARSET;

    if (lpszFamily != nullptr)
    {
        Family = MByteToWChar(lpszFamily);
        if (Family == nullptr)
            return FALSE;
    }
    else
    {
        Family = nullptr;
    }

    Result = LepEnumFontFamiliesW(hdc, Family, LepEnumFontCallbackAFromW, (LPARAM)&Param);
    FreeString(Family);

    return Result;
}

int NTAPI LepEnumFontsW(HDC hdc, PCWSTR lpFaceName, FONTENUMPROCW lpProc, LPARAM lParam)
{
    GDI_ENUM_FONT_PARAM Param;
    LOGFONTW            LocalLogFont;
    PLepGlobalData       GlobalData = LepGetGlobalData();

    if (NT_FAILED(Param.Prepare(GlobalData)))
        return FALSE;

    Param.Callback      = lpProc;
    Param.GlobalData    = GlobalData;
    Param.lParam        = lParam;
    Param.Charset       = DEFAULT_CHARSET;

    return GlobalData->EnumFontsW(hdc, lpFaceName, LepEnumFontCallbackW, (LPARAM)&Param);
}

int NTAPI LepEnumFontsA(HDC hdc, PCSTR lpFaceName, FONTENUMPROCA lpProc, LPARAM lParam)
{
    INT                 Result;
    PWSTR               FaceName;
    GDI_ENUM_FONT_PARAM Param;
    LOGFONTW            LocalLogFont;
    PLepGlobalData       GlobalData = LepGetGlobalData();

    Param.Callback      = lpProc;
    Param.GlobalData    = GlobalData;
    Param.lParam        = lParam;
    Param.Charset       = DEFAULT_CHARSET;

    if (lpFaceName != nullptr)
    {
        FaceName = MByteToWChar(lpFaceName);
        if (FaceName == nullptr)
            return FALSE;
    }
    else
    {
        FaceName = nullptr;
    }

    Result = LepEnumFontsW(hdc, FaceName, LepEnumFontCallbackAFromW, (LPARAM)&Param);
    FreeString(FaceName);

    return Result;
}

HFONT
NTAPI
LepNtGdiHfontCreateWorker(
    PVOID               OriginalRoutine,
    PENUMLOGFONTEXDVW   EnumLogFont,
    ULONG               SizeOfEnumLogFont,
    LONG                LogFontType,
    LONG                Unknown,
    PVOID               FreeListLocalFont
)
{
    PENUMLOGFONTEXDVW   enumlfex;
    PLepGlobalData       GlobalData = LepGetGlobalData();

    if (EnumLogFont != nullptr && IsGdiHookBypassed() == FALSE) LOOP_ONCE
    {
        ULONG_PTR Charset;

        Charset = EnumLogFont->elfEnumLogfontEx.elfLogFont.lfCharSet;

        if (Charset != ANSI_CHARSET && Charset != DEFAULT_CHARSET)
            break;

        enumlfex = (PENUMLOGFONTEXDVW)AllocStack(SizeOfEnumLogFont);

        CopyMemory(enumlfex, EnumLogFont, SizeOfEnumLogFont);

        enumlfex->elfEnumLogfontEx.elfLogFont.lfCharSet = GlobalData->GetLepb()->DefaultCharset;

        //if (GdiGetCodePage == NULL)
        //CopyStruct(enumlfex->elfEnumLogfontEx.elfLogFont.lfFaceName, GlobalData->GetLepb()->DefaultFaceName, LF_FACESIZE);
        //AllocConsole();
        //PrintConsoleW(L"%s\n", enumlfex.elfEnumLogfontEx.elfLogFont.lfFaceName);

        EnumLogFont = enumlfex;
    }

    return ((HFONT (NTAPI *)(PENUMLOGFONTEXDVW, ULONG, LONG, LONG, PVOID))OriginalRoutine)(
        EnumLogFont,
        SizeOfEnumLogFont,
        LogFontType,
        Unknown,
        FreeListLocalFont);
}

HFONT
NTAPI
LepNtGdiHfontCreate(
    PENUMLOGFONTEXDVW   EnumLogFont,
    ULONG               SizeOfEnumLogFont,
    LONG                LogFontType,
    LONG                Unknown,
    PVOID               FreeListLocalFont
)
{
    PLepGlobalData GlobalData = LepGetGlobalData();

    return LepNtGdiHfontCreateWorker(
        GlobalData->HookStub.StubNtGdiHfontCreate,
        EnumLogFont,
        SizeOfEnumLogFont,
        LogFontType,
        Unknown,
        FreeListLocalFont);
}

#if ML_AMD64
HFONT
HPCALL
LepHpNtGdiHfontCreate(
    HPARGS
    PENUMLOGFONTEXDVW   EnumLogFont,
    ULONG               SizeOfEnumLogFont,
    LONG                LogFontType,
    LONG                Unknown,
    PVOID               FreeListLocalFont
)
{
    PVOID Original;

    HpSetFilterAction(BlockSystemCall);

    Original = HpGetSystemCallOriginal(WIN32K_NtGdiHfontCreate);
    if (Original == nullptr)
        return nullptr;

    return LepNtGdiHfontCreateWorker(
        Original,
        EnumLogFont,
        SizeOfEnumLogFont,
        LogFontType,
        Unknown,
        FreeListLocalFont);
}
#endif

ULONG
NTAPI
LepQueryFontAssocStatus()
{
    return 0;
}

API_POINTER(SelectObject)  StubSelectObject;

HGDIOBJ NTAPI LepSelectObject(HDC hdc, HGDIOBJ h)
{
    HGDIOBJ obj;

    obj = StubSelectObject(hdc, h);

    switch (GdiGetCodePage(hdc))
    {
        //case 0x3A4:
        case 0x3A8:
        {
            ULONG objtype = GetObjectType(h);

            union
            {
                LOGFONTW lf;
            };

            switch (objtype)
            {
                case OBJ_FONT:
                    GetObjectW(h, sizeof(lf), &lf);
                    ExceptionBox(lf.lfFaceName, L"FUCK FACE");
                    break;

                default:
                    return obj;
            }

            break;
        }
    }

    return obj;
}

/************************************************************************
  init
************************************************************************/

#if ML_AMD64
static BOOL IsX64SyscallStub(PVOID Routine);
#endif

PVOID FindNtGdiHfontCreate(PVOID Gdi32)
{
    PVOID CreateFontIndirectExW, NtGdiHfontCreate;

    CreateFontIndirectExW = LookupExportTable(Gdi32, GDI32_CreateFontIndirectExW);

    NtGdiHfontCreate = WalkOpCodeT(CreateFontIndirectExW, 0xA0,
                            WalkOpCodeM(Buffer, OpLength, Ret)
                            {
                                switch (Buffer[0])
                                {
                                    case CALL:
                                        Buffer = GetCallDestination(Buffer);
#if ML_AMD64
                                        if (IsX64SyscallStub(Buffer) == FALSE)
                                            break;
#else
                                        if (IsSystemCall(Buffer) == FALSE)
                                            break;
#endif

                                        Ret = Buffer;
                                        //return STATUS_SUCCESS;
                                        break;
                                }

                                return STATUS_NOT_FOUND;
                            }
                        );

    return NtGdiHfontCreate;
}

#if ML_AMD64
static BOOL IsX64SyscallStub(PVOID Routine)
{
    PBYTE Function;

    Function = (PBYTE)Routine;
    if (Function == nullptr)
        return FALSE;

    if (Function[0] != 0x4C ||
        Function[1] != 0x8B ||
        Function[2] != 0xD1 ||
        Function[3] != 0xB8)
        return FALSE;

    for (ULONG_PTR i = 8; i != 0x20; ++i)
        if (Function[i] == 0x0F && Function[i + 1] == 0x05 && Function[i + 2] == 0xC3)
            return TRUE;

    return FALSE;
}

static NTSTATUS CheckX64GdiSyscallRoutine(PVOID Routine, ULONG IdForLog)
{
    if (Routine == nullptr)
        return STATUS_NOT_FOUND;

    if (!IsX64SyscallStub(Routine))
    {
#if ENABLE_LOG
        PBYTE Bytes = (PBYTE)Routine;
        WriteLog(L"gdi32 x64 export is not syscall stub hash=%08X routine=%p bytes=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
            IdForLog,
            Routine,
            Bytes[0], Bytes[1], Bytes[2], Bytes[3],
            Bytes[4], Bytes[5], Bytes[6], Bytes[7],
            Bytes[8], Bytes[9], Bytes[10], Bytes[11]);
#endif
        return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}
#endif


/************************************************************************
  init end
************************************************************************/

NTSTATUS LepGlobalData::HookGdi32Routines(PVOID Gdi32)
{
    PVOID NtGdiHfontCreate, Fms;
    PVOID QueryFontAssocStatus;
    NTSTATUS Status;

    *(PVOID *)&GdiGetCodePage = GetRoutineAddress(Gdi32, "GdiGetCodePage");
    QueryFontAssocStatus = nullptr;

    if (this->HasWin32U)
    {
        HMODULE Win32uMod = (HMODULE)Nt_GetModuleHandle(L"win32u.dll");
        if (Win32uMod == nullptr)
            return STATUS_NOT_FOUND;
        NtGdiHfontCreate = Nt_GetProcAddress(Win32uMod, "NtGdiHfontCreate");
        if (NtGdiHfontCreate == nullptr)
            return STATUS_NOT_FOUND;
    }
    else
    {
        NtGdiHfontCreate = FindNtGdiHfontCreate(Gdi32);
        if (NtGdiHfontCreate == nullptr)
            return STATUS_NOT_FOUND;
    }

    if (GetLepb()->AnsiCodePage == CP_SHIFTJIS)
    {
        PVOID Gdi32Full = Nt_GetModuleHandle(L"gdi32full.dll");

        if (Gdi32Full != nullptr)
            QueryFontAssocStatus = LookupExportTable(Gdi32Full, GDI32_QueryFontAssocStatus);

        if (QueryFontAssocStatus == nullptr)
            QueryFontAssocStatus = LookupExportTable(Gdi32, GDI32_QueryFontAssocStatus);

        if (QueryFontAssocStatus == nullptr)
            return STATUS_NOT_FOUND;
    }

    RtlInitializeCriticalSectionAndSpinCount(&HookRoutineData.Gdi32.GdiLock, 4000);

    if (HookStub.StubNtUserCreateWindowEx != nullptr)
    {
        InitFontCharsetInfo();
    }

    //Fms = Ldr::LoadDll(L"fms.dll");
    //if (Fms == nullptr)
    //{
    //    return STATUS_DLL_NOT_FOUND;
    //}
    //LdrAddRefDll(LDR_ADDREF_DLL_PIN, Fms);

#if ML_AMD64
    Status = CheckX64GdiSyscallRoutine(NtGdiHfontCreate, WIN32K_NtGdiHfontCreate);
    FAIL_RETURN(Status);

    Status = HpAddSystemCallByRoutine(NtGdiHfontCreate, WIN32K_NtGdiHfontCreate);
    FAIL_RETURN(Status);

    Status = HpAddSystemCallFilter(WIN32K_NtGdiHfontCreate, LepHpNtGdiHfontCreate, this);
    FAIL_RETURN(Status);

    if (QueryFontAssocStatus != nullptr)
    {
        Mp::PATCH_MEMORY_DATA FontAssocPatch[] =
        {
            Mp::FunctionJumpVa(
                QueryFontAssocStatus,
                LepQueryFontAssocStatus,
                &HookStub.StubQueryFontAssocStatus,
                LEP_FUNCTION_JUMP_OP),
        };

        Status = Mp::PatchMemory(FontAssocPatch, countof(FontAssocPatch));
        FAIL_RETURN(Status);
    }

    PVOID EnumFontModule = Nt_GetModuleHandle(L"gdi32full.dll");
    if (EnumFontModule == nullptr)
        EnumFontModule = Gdi32;

#define LepHookEnumFontFromEAT(_Base, _Name) Mp::FunctionJumpVa(LookupExportTable(_Base, GDI32_##_Name), Lep##_Name, &HookStub.Stub##_Name, Mp::OpJumpIndirect)

    Mp::PATCH_MEMORY_DATA p[] =
    {
        LepHookFromEAT(Gdi32, GDI32, GetStockObject),
        LepHookFromEAT(Gdi32, GDI32, DeleteObject),
        LepHookFromEAT(Gdi32, GDI32, CreateCompatibleDC),
        LepHookEnumFontFromEAT(EnumFontModule, EnumFontsW),
        LepHookEnumFontFromEAT(EnumFontModule, EnumFontsA),
        LepHookEnumFontFromEAT(EnumFontModule, EnumFontFamiliesA),
        LepHookEnumFontFromEAT(EnumFontModule, EnumFontFamiliesW),
        LepHookEnumFontFromEAT(EnumFontModule, EnumFontFamiliesExA),
        LepHookEnumFontFromEAT(EnumFontModule, EnumFontFamiliesExW),
    };

#undef LepHookEnumFontFromEAT
#else
    Mp::PATCH_MEMORY_DATA p[] =
    {
        LepHookFromEAT(Gdi32, GDI32, GetStockObject),
        LepHookFromEAT(Gdi32, GDI32, DeleteObject),
        //LepHookFromEAT(Gdi32, GDI32, CreateFontIndirectExW),
        LepHookFromEAT(Gdi32, GDI32, CreateCompatibleDC),
        LepHookFromEAT(Gdi32, GDI32, EnumFontsW),
        LepHookFromEAT(Gdi32, GDI32, EnumFontsA),
        LepHookFromEAT(Gdi32, GDI32, EnumFontFamiliesA),
        LepHookFromEAT(Gdi32, GDI32, EnumFontFamiliesW),
        LepHookFromEAT(Gdi32, GDI32, EnumFontFamiliesExA),
        LepHookFromEAT(Gdi32, GDI32, EnumFontFamiliesExW),

        LepFunctionJump(NtGdiHfontCreate),

        //Mp::MemoryPatchVa((ULONG_PTR)LepFmsEnumFontFamiliesExW, sizeof(ULONG_PTR), LookupImportTable(Fms, "GDI32.dll", GDI32_EnumFontFamiliesExW)),

        //Mp::FunctionJumpVa(LookupExportTable(Gdi32, GDI32_SelectObject), LepSelectObject, &StubSelectObject),
    };
#endif

#if !ML_AMD64
    if (QueryFontAssocStatus != nullptr)
    {
        Mp::PATCH_MEMORY_DATA FontAssocPatch[] =
        {
            Mp::FunctionJumpVa(QueryFontAssocStatus, LepQueryFontAssocStatus, &HookStub.StubQueryFontAssocStatus, LEP_FUNCTION_JUMP_OP),
        };

        Status = Mp::PatchMemory(FontAssocPatch, countof(FontAssocPatch));
        FAIL_RETURN(Status);
    }
#endif

    return Mp::PatchMemory(p, countof(p));
}

NTSTATUS LepGlobalData::UnHookGdi32Routines()
{
#if ML_AMD64
    HpRemoveSystemCallFilter(WIN32K_NtGdiHfontCreate, LepHpNtGdiHfontCreate);
#endif

    Mp::RestoreMemory(HookStub.StubGetStockObject);
    Mp::RestoreMemory(HookStub.StubDeleteObject);
    Mp::RestoreMemory(HookStub.StubCreateCompatibleDC);
    Mp::RestoreMemory(HookStub.StubEnumFontFamiliesExA);
    Mp::RestoreMemory(HookStub.StubEnumFontFamiliesExW);
    Mp::RestoreMemory(HookStub.StubEnumFontsA);
    Mp::RestoreMemory(HookStub.StubEnumFontsW);
    Mp::RestoreMemory(HookStub.StubNtGdiHfontCreate);
    Mp::RestoreMemory(HookStub.StubQueryFontAssocStatus);

    return 0;
}
