#ifndef LEP_BROKER_PROTOCOL_H
#define LEP_BROKER_PROTOCOL_H

#include "../LocaleEmulatorPlus/LocaleEmulatorPlus.h"

#define LEP_BROKER_CONFIG_MAGIC TAG4('LBC1')
#define LEP_BROKER_CONFIG_VERSION 4

typedef struct LEP_BROKER_CONFIG
{
    ULONG Magic;
    ULONG Version;
    ULONG Size;
    ULONG PayloadSize;
    ULONG Result;
    ULONG ThreadId;
    ULONG InjectionFlags;
    BYTE Payload[1];
} LEP_BROKER_CONFIG, *PLEP_BROKER_CONFIG;

#endif
