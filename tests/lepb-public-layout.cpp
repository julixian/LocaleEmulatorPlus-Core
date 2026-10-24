#include <Windows.h>
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>
import LocaleEmulatorPlus;

extern "C" void LepGetCoreLayout(ULONG* values);

int wmain()
{
    // Compare separate translation units with the independently declared ABI.
    ULONG actual[40] = {};
    LepGetCoreLayout(actual);
    const ULONG expected[] = {
        LEP::LEP_ENVIRONMENT_VERSION,
        sizeof(LEP::LEPB), sizeof(PROCESS_INFORMATION),
        offsetof(LEP::LEPB, Size), offsetof(LEP::LEPB, Version), offsetof(LEP::LEPB, AnsiCodePage),
        offsetof(LEP::LEPB, OemCodePage), offsetof(LEP::LEPB, LocaleId), offsetof(LEP::LEPB, FontCharset),
        offsetof(LEP::LEPB, RegistryRedirectionMode), offsetof(LEP::LEPB, HookUILanguageMode),
        offsetof(LEP::LEPB, TimezoneId), offsetof(LEP::LEPB, StandardNameOverride), offsetof(LEP::LEPB, DaylightNameOverride),
        offsetof(PROCESS_INFORMATION, hProcess), offsetof(PROCESS_INFORMATION, hThread),
        offsetof(PROCESS_INFORMATION, dwProcessId), offsetof(PROCESS_INFORMATION, dwThreadId),
    };
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    {
        if (actual[i] != expected[i]) { printf("ABI mismatch at %u: %lu != %lu\n", i, actual[i], expected[i]); return 1; }
    }
    if (GetEnvironmentVariableW(L"LEP_PUBLIC_API_TEST", nullptr, 0) != 0)
    {
        TIME_ZONE_INFORMATION timezone{};
        if (GetTimeZoneInformation(&timezone) == TIME_ZONE_ID_INVALID || GetACP() != 932 ||
            timezone.Bias != -540 || wcscmp(timezone.StandardName, L"東京標準時") != 0) return 2;
    }
    puts("Public/Core ABI matched; caller checks passed");
    return 0;
}
