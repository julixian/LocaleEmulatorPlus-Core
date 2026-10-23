#pragma once

#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NON_CONFORMING_SWPRINTFS
#define _CRT_NONSTDC_NO_WARNINGS
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 0
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT 0
#define _CRT_SECURE_CPP_OVERLOAD_SECURE_NAMES 0

// Preserve the SDK selection order used by the original support layer.
#ifndef _WINSOCKAPI_
#include <ws2spi.h>
#endif
#include <intsafe.h>

#include "Macros.h"
#include "Constexpr.h"

#include <Windows.h>
#pragma warning(push)
#pragma warning(disable:4005)
#include <ntstatus.h>
#pragma warning(pop)

typedef void * POINTER_64 HANDLE64;
typedef HANDLE64 *PHANDLE64;

#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>
#include <conio.h>
#include <stdlib.h>
#include <malloc.h>
#include <time.h>
#include <signal.h>
#include <locale.h>
#include <process.h>
#include <tchar.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <Shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")
