#include "Environment.h"

namespace
{
    struct LegacyEnvironmentV3
    {
        LEP_RUNTIME_ENVIRONMENT Base;
        WCHAR TimeZoneId[128];
    };
    static_assert(sizeof(LegacyEnvironmentV3) == 460, "Legacy v3 layout mismatch");

    struct RegistryTimeZone
    {
        LONG Bias;
        LONG StandardBias;
        LONG DaylightBias;
        SYSTEMTIME StandardDate;
        SYSTEMTIME DaylightDate;
    };
    static_assert(sizeof(RegistryTimeZone) == 44, "Registry TZI layout mismatch");

    bool ValidTimeZoneId(PCWSTR Id, ULONG Capacity, bool AllowEmpty)
    {
        if (!AllowEmpty && Id[0] == 0)
            return false;
        for (ULONG Index = 0; Index != Capacity; ++Index)
        {
            if (Id[Index] == 0)
                return true;
            if (Id[Index] == L'\\' || Id[Index] == L'/')
                return false;
        }
        return false;
    }

    NTSTATUS OpenTimeZone(PCWSTR Id, HANDLE* Key)
    {
        static const WCHAR Prefix[] = L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\";
        WCHAR Path[countof(Prefix) + 128];
        CopyMemory(Path, Prefix, sizeof(Prefix) - sizeof(WCHAR));
        ULONG Length = (ULONG)StrLengthW(Id);
        CopyMemory(Path + countof(Prefix) - 1, Id, (Length + 1) * sizeof(WCHAR));
        UNICODE_STRING Name;
        OBJECT_ATTRIBUTES Attributes;
        RtlInitUnicodeString(&Name, Path);
        InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
        return NtOpenKey(Key, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &Attributes);
    }

    NTSTATUS ReadValue(HANDLE Key, PCWSTR Name, ULONG Type, PVOID Data, ULONG Capacity, PULONG Length)
    {
        ULONG Storage[140];
        auto Value = (PKEY_VALUE_PARTIAL_INFORMATION)Storage;
        UNICODE_STRING ValueName;
        ULONG ResultLength = 0;
        RtlInitUnicodeString(&ValueName, Name);
        NTSTATUS Status = NtQueryValueKey(Key, &ValueName, KeyValuePartialInformation,
                                         Value, sizeof(Storage), &ResultLength);
        if (NT_FAILED(Status))
            return Status;
        ULONG Header = FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data);
        if (ResultLength < Header || Value->DataLength > ResultLength - Header)
            return STATUS_INFO_LENGTH_MISMATCH;
        if (Value->Type != Type)
            return STATUS_OBJECT_TYPE_MISMATCH;
        if (Value->DataLength > Capacity)
            return STATUS_BUFFER_TOO_SMALL;
        CopyMemory(Data, Value->Data, Value->DataLength);
        *Length = Value->DataLength;
        return STATUS_SUCCESS;
    }

    NTSTATUS ReadName(HANDLE Key, PCWSTR Value, WCHAR (&Name)[32])
    {
        ZeroMemory(Name, sizeof(Name));
        ULONG Length = 0;
        NTSTATUS Status = ReadValue(Key, Value, REG_SZ, Name, sizeof(Name), &Length);
        if (NT_FAILED(Status))
            return Status;
        if ((Length & 1) != 0 || !LepStringTerminated(Name, countof(Name)))
            return STATUS_INVALID_PARAMETER;
        return STATUS_SUCCESS;
    }

    TIME_FIELDS ConvertTransition(const SYSTEMTIME& Date)
    {
        TIME_FIELDS Fields = {};
        Fields.Year = Date.wYear;
        Fields.Month = Date.wMonth;
        Fields.Day = Date.wDay;
        Fields.Hour = Date.wHour;
        Fields.Minute = Date.wMinute;
        Fields.Second = Date.wSecond;
        Fields.Milliseconds = Date.wMilliseconds;
        Fields.Weekday = Date.wDayOfWeek;
        return Fields;
    }

    void NormalizeTokyoName(PRTL_TIME_ZONE_INFORMATION Zone)
    {
        static const WCHAR Name[] = L"東京標準時";
        ZeroMemory(Zone->StandardName, sizeof(Zone->StandardName));
        ZeroMemory(Zone->DaylightName, sizeof(Zone->DaylightName));
        CopyMemory(Zone->StandardName, Name, sizeof(Name));
        CopyMemory(Zone->DaylightName, Name, sizeof(Name));
    }

    NTSTATUS ResolveTimeZone(const LEPB* Input, PRTL_TIME_ZONE_INFORMATION Zone)
    {
        HANDLE Key = nullptr;
        NTSTATUS Status = OpenTimeZone(Input->TimeZoneId, &Key);
        if (NT_FAILED(Status))
            return Status;

        RegistryTimeZone Tzi = {};
        ULONG Length = 0;
        Status = ReadValue(Key, L"TZI", REG_BINARY, &Tzi, sizeof(Tzi), &Length);
        if (NT_SUCCESS(Status) && Length != sizeof(Tzi))
            Status = STATUS_INFO_LENGTH_MISMATCH;
        if (NT_SUCCESS(Status))
            Status = ReadName(Key, L"Std", Zone->StandardName);
        if (NT_SUCCESS(Status))
            Status = ReadName(Key, L"Dlt", Zone->DaylightName);
        NtClose(Key);
        if (NT_FAILED(Status))
            return Status;

        Zone->Bias = Tzi.Bias;
        Zone->StandardBias = Tzi.StandardBias;
        Zone->DaylightBias = Tzi.DaylightBias;
        Zone->StandardStart = ConvertTransition(Tzi.StandardDate);
        Zone->DaylightStart = ConvertTransition(Tzi.DaylightDate);
        if (StrICompareW(Input->TimeZoneId, L"Tokyo Standard Time") == 0)
            NormalizeTokyoName(Zone);
        if (Input->StandardNameOverride[0] != 0)
            CopyMemory(Zone->StandardName, Input->StandardNameOverride, sizeof(Zone->StandardName));
        if (Input->DaylightNameOverride[0] != 0)
            CopyMemory(Zone->DaylightName, Input->DaylightNameOverride, sizeof(Zone->DaylightName));
        return STATUS_SUCCESS;
    }
}

NTSTATUS LepResolveEnvironment(const void* Input, PLEP_RUNTIME_ENVIRONMENT Output)
{
    if (Input == nullptr || Output == nullptr)
        return STATUS_INVALID_PARAMETER;
    const ULONG* Header = (const ULONG*)Input;
    ULONG Size = Header[0];
    if (Size < 2 * sizeof(ULONG))
        return STATUS_INVALID_PARAMETER;
    ULONG Version = Header[1];
    LEP_RUNTIME_ENVIRONMENT Resolved = {};

    if (Version == LEP_ENVIRONMENT_VERSION && Size == sizeof(LEPB))
    {
        auto Environment = (const LEPB*)Input;
        if (!ValidTimeZoneId(Environment->TimeZoneId, countof(Environment->TimeZoneId), false) ||
            !LepStringTerminated(Environment->StandardNameOverride, countof(Environment->StandardNameOverride)) ||
            !LepStringTerminated(Environment->DaylightNameOverride, countof(Environment->DaylightNameOverride)))
            return STATUS_INVALID_PARAMETER;
        CopyMemory(&Resolved.AnsiCodePage, &Environment->AnsiCodePage, 6 * sizeof(ULONG));
        if (Resolved.RegistryRedirectionMode > LEP_REGISTRY_REDIRECTION_MODE_MAX ||
            Resolved.HookUILanguageMode > LEP_UI_LANGUAGE_MODE_MAX)
            return STATUS_INVALID_PARAMETER;
        NTSTATUS Status = ResolveTimeZone(Environment, &Resolved.Timezone);
        if (NT_FAILED(Status))
            return Status;
    }
    else if ((Version == 2 && Size == sizeof(LEP_RUNTIME_ENVIRONMENT)) ||
             (Version == 3 && Size == sizeof(LegacyEnvironmentV3)))
    {
        CopyMemory(&Resolved, Input, sizeof(Resolved));
        if (Version == 3)
        {
            auto Environment = (const LegacyEnvironmentV3*)Input;
            if (!ValidTimeZoneId(Environment->TimeZoneId, countof(Environment->TimeZoneId), true))
                return STATUS_INVALID_PARAMETER;
            if (Environment->TimeZoneId[0] != 0)
            {
                bool Tokyo = StrICompareW(Environment->TimeZoneId, L"Tokyo Standard Time") == 0;
                if (Tokyo && (Resolved.Timezone.Bias != -540 || Resolved.Timezone.StandardBias != 0))
                    return STATUS_INVALID_PARAMETER;
                HANDLE Key = nullptr;
                NTSTATUS Status = OpenTimeZone(Environment->TimeZoneId, &Key);
                if (NT_FAILED(Status))
                    return Status;
                NtClose(Key);
                if (Tokyo)
                    NormalizeTokyoName(&Resolved.Timezone);
            }
        }
    }
    else
        return STATUS_INVALID_PARAMETER;

    Resolved.Size = sizeof(Resolved);
    Resolved.Version = LEP_RUNTIME_ENVIRONMENT_VERSION;
    if (!LepValidateRuntimeEnvironment(&Resolved))
        return STATUS_INVALID_PARAMETER;
    *Output = Resolved;
    return STATUS_SUCCESS;
}
