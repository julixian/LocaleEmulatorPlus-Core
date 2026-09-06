#ifndef LEP_BROKER_PROTOCOL_H
#define LEP_BROKER_PROTOCOL_H

#include "../LocaleEmulatorPlus/LocaleEmulatorPlus.h"

#define LEP_BROKER_CONFIG_MAGIC TAG4('LBC1')
#define LEP_BROKER_CONFIG_VERSION 3

typedef struct LEP_BROKER_CONFIG
{
    ULONG Magic;
    ULONG Version;
    ULONG Size;
    ULONG ExtraSize;
    ULONG Result;
    ULONG ThreadId;
    ULONG InjectionFlags;
    WCHAR DllPath[MAX_NTPATH];
    LEPB  Environment;
} LEP_BROKER_CONFIG, *PLEP_BROKER_CONFIG;

#endif
