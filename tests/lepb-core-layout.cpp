#include "../LocaleEmulatorPlus/stdafx.h"

extern "C" void LepGetCoreLayout(ULONG* values)
{
    const ULONG layout[] = {
        LEP_ENVIRONMENT_VERSION,
        sizeof(LEPB), sizeof(PROCESS_INFORMATION),
        offsetof(LEPB, Size), offsetof(LEPB, Version), offsetof(LEPB, AnsiCodePage),
        offsetof(LEPB, OemCodePage), offsetof(LEPB, LocaleId), offsetof(LEPB, FontCharset),
        offsetof(LEPB, RegistryRedirectionMode), offsetof(LEPB, HookUILanguageMode),
        offsetof(LEPB, TimezoneId), offsetof(LEPB, StandardNameOverride), offsetof(LEPB, DaylightNameOverride),
        offsetof(PROCESS_INFORMATION, hProcess), offsetof(PROCESS_INFORMATION, hThread),
        offsetof(PROCESS_INFORMATION, dwProcessId), offsetof(PROCESS_INFORMATION, dwThreadId),
    };
    CopyMemory(values, layout, sizeof(layout));
}
