// The runner force-includes the selected support header before this file.
#include <stdio.h>
#include <stddef.h>

// Golden values from the removed export-hash tables; these also require the
// replacement to remain usable as a compile-time constant.
static_assert(lep::HashExportName("") == 0, "empty export name");
static_assert(lep::HashExportName("NtClose") == 0xB82DD68Cu, "NTDLL hash");
static_assert(lep::HashExportName("LdrLoadDll") == 0x8F8ED6B0u, "loader hash");
static_assert(lep::HashExportName("NtUserCreateWindowEx") == 0xD7126686u, "WIN32K hash");
static_assert(lep::HashExportName("CreateFontIndirectExW") == 0xBF2C92A9u, "GDI hash");

int main()
{
#define SIZE(type) printf("sizeof(" #type ")=%Iu\n", sizeof(type))
#define OFFSET(type, member) printf("offsetof(" #type "," #member ")=%Iu\n", offsetof(type, member))
    SIZE(UNICODE_STRING);
    SIZE(OBJECT_ATTRIBUTES);
    SIZE(OBJECT_BASIC_INFORMATION);
    SIZE(PEB_BASE);
    SIZE(TEB_BASE);
    SIZE(PS_CREATE_INFO);
    SIZE(PS_ATTRIBUTE_LIST);
    SIZE(TIME_FIELDS);
    SIZE(RTL_TIME_ZONE_INFORMATION);
    SIZE(CPTABLEINFO);
    SIZE(NLSTABLEINFO);
    SIZE(TEB_ACTIVE_FRAME);
    SIZE(NativeFile);
    SIZE(lep::String);
    SIZE(lep::DynamicArray<BYTE>);
    OFFSET(PEB_BASE, Ldr);
    OFFSET(PEB_BASE, ProcessHeap);
    OFFSET(PEB_BASE, AnsiCodePageData);
    OFFSET(PEB_BASE, OemCodePageData);
    OFFSET(PEB_BASE, UnicodeCaseTableData);
    OFFSET(TEB_BASE, ProcessEnvironmentBlock);
    OFFSET(TEB_BASE, CurrentLocale);
    OFFSET(OBJECT_BASIC_INFORMATION, DesiredAccess);
    OFFSET(RTL_TIME_ZONE_INFORMATION, StandardStart);
    OFFSET(RTL_TIME_ZONE_INFORMATION, DaylightStart);
    OFFSET(PS_ATTRIBUTE_LIST, Attributes);
#undef SIZE
#undef OFFSET
    return 0;
}
