#include <Windows.h>
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>
import LocaleEmulatorPlus;

extern "C" void LepGetCoreLayout(ULONG* values);

int wmain()
{
    // Compare separate translation units: Core's private legacy headers
    // remain in their normal language mode, outside the C++20 module.
    ULONG actual[40] = {};
    LepGetCoreLayout(actual);
    const ULONG expected[] = {
        LEP::LEP_ENVIRONMENT_VERSION,
        sizeof(LEP::LEPB), sizeof(LEP::TIME_FIELDS), sizeof(LEP::RTL_TIME_ZONE_INFORMATION), sizeof(LEP::ML_PROCESS_INFORMATION),
        offsetof(LEP::LEPB, Size), offsetof(LEP::LEPB, Version), offsetof(LEP::LEPB, AnsiCodePage),
        offsetof(LEP::LEPB, OemCodePage), offsetof(LEP::LEPB, LocaleID), offsetof(LEP::LEPB, DefaultCharset),
        offsetof(LEP::LEPB, RegistryRedirectionMode), offsetof(LEP::LEPB, HookUILanguageMode),
        offsetof(LEP::LEPB, Timezone), offsetof(LEP::LEPB, TimeZoneId),
        offsetof(LEP::TIME_FIELDS, Year), offsetof(LEP::TIME_FIELDS, Month), offsetof(LEP::TIME_FIELDS, Day),
        offsetof(LEP::TIME_FIELDS, Hour), offsetof(LEP::TIME_FIELDS, Minute), offsetof(LEP::TIME_FIELDS, Second),
        offsetof(LEP::TIME_FIELDS, Milliseconds), offsetof(LEP::TIME_FIELDS, Weekday),
        offsetof(LEP::RTL_TIME_ZONE_INFORMATION, Bias), offsetof(LEP::RTL_TIME_ZONE_INFORMATION, StandardName),
        offsetof(LEP::RTL_TIME_ZONE_INFORMATION, StandardStart), offsetof(LEP::RTL_TIME_ZONE_INFORMATION, StandardBias),
        offsetof(LEP::RTL_TIME_ZONE_INFORMATION, DaylightName), offsetof(LEP::RTL_TIME_ZONE_INFORMATION, DaylightStart),
        offsetof(LEP::RTL_TIME_ZONE_INFORMATION, DaylightBias),
    };
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    {
        if (actual[i] != expected[i]) { printf("ABI mismatch at %u: %lu != %lu\n", i, actual[i], expected[i]); return 1; }
    }
    if (GetEnvironmentVariableW(L"LEP_PUBLIC_API_TEST", nullptr, 0) != 0)
    {
        TIME_ZONE_INFORMATION timezone{};
        if (GetTimeZoneInformation(&timezone) == TIME_ZONE_ID_INVALID || GetACP() != 932 ||
            timezone.Bias != -540 || wcscmp(timezone.StandardName, L"\u6771\u4EAC\u6A19\u6E96\u6642") != 0) return 2;
    }
    puts("Public/Core ABI matched; caller checks passed");
    return 0;
}
