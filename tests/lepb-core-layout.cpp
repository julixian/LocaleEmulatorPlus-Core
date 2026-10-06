#include "../LocaleEmulatorPlus/stdafx.h"

extern "C" void LepGetCoreLayout(ULONG* values)
{
    const ULONG layout[] = {
        LEP_ENVIRONMENT_VERSION,
        sizeof(LEPB), sizeof(TIME_FIELDS), sizeof(RTL_TIME_ZONE_INFORMATION), sizeof(ML_PROCESS_INFORMATION),
        offsetof(LEPB, Size), offsetof(LEPB, Version), offsetof(LEPB, AnsiCodePage),
        offsetof(LEPB, OemCodePage), offsetof(LEPB, LocaleID), offsetof(LEPB, DefaultCharset),
        offsetof(LEPB, RegistryRedirectionMode), offsetof(LEPB, HookUILanguageMode),
        offsetof(LEPB, Timezone), offsetof(LEPB, TimeZoneId),
        offsetof(TIME_FIELDS, Year), offsetof(TIME_FIELDS, Month), offsetof(TIME_FIELDS, Day),
        offsetof(TIME_FIELDS, Hour), offsetof(TIME_FIELDS, Minute), offsetof(TIME_FIELDS, Second),
        offsetof(TIME_FIELDS, Milliseconds), offsetof(TIME_FIELDS, Weekday),
        offsetof(RTL_TIME_ZONE_INFORMATION, Bias), offsetof(RTL_TIME_ZONE_INFORMATION, StandardName),
        offsetof(RTL_TIME_ZONE_INFORMATION, StandardStart), offsetof(RTL_TIME_ZONE_INFORMATION, StandardBias),
        offsetof(RTL_TIME_ZONE_INFORMATION, DaylightName), offsetof(RTL_TIME_ZONE_INFORMATION, DaylightStart),
        offsetof(RTL_TIME_ZONE_INFORMATION, DaylightBias),
    };
    CopyMemory(values, layout, sizeof(layout));
}
