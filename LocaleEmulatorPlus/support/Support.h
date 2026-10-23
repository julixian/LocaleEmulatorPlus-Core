// Internal umbrella: declaration order and namespace scopes are intentional.
// Include this header rather than individual declaration fragments.
// Implementations are compiled separately into LepSupport.lib.
#ifndef _MYLIBRARY_H_9fb91137_38ac_4d5e_8572_b6e12d23cb15_
#define _MYLIBRARY_H_9fb91137_38ac_4d5e_8572_b6e12d23cb15_


#define ML_DISABLE_THIRD_LIB_UCL 1

#ifndef _MYLIBRARYUSER_H_e5b0d13b_823a_4f24_88ec_6515f58a7140_
#define _MYLIBRARYUSER_H_e5b0d13b_823a_4f24_88ec_6515f58a7140_


#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NON_CONFORMING_SWPRINTFS
#define _CRT_NONSTDC_NO_WARNINGS
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 0
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT 0
#define _CRT_SECURE_CPP_OVERLOAD_SECURE_NAMES 0

#ifndef _WINSOCKAPI_
    #include <ws2spi.h>
#endif

#include <intsafe.h>

#include "base/Macros.h"
#include "base/Constexpr.h"


//    #include <winsock2.h>
    #include <Windows.h>
    #include <Wininet.h>

    #pragma warning(disable:4005)

    #include <ntstatus.h>

    #pragma warning(default:4005)


typedef void * POINTER_64 HANDLE64;
typedef HANDLE64 *PHANDLE64;

#include "generated/ExportHashes.h"
#pragma comment(lib, "Shlwapi.lib")

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

#include <tchar.h>

#include <Shlwapi.h>

#include "runtime/MemoryPrimitives.h"
#include "runtime/Utility.h"

#include "native/Native.h"
#include "runtime/Console.h"
//#include "Hook.h"
#include "pe/ImageTypes.h"
#include "runtime/Audio.h"
#include "runtime/Compression.h"
#include "native/AppCompat.h"

#include "pe/Image.h"
#include "runtime/File.h"
#include "patch/Decoder.h"

#if ML_SUPPORT_LAMBDA

#include "runtime/HeapDiagnostics.h"

#endif // ML_SUPPORT_LAMBDA

// class

#if CPP_DEFINED


ML_NAMESPACE

#include "runtime/ContainerBase.h"
#include "runtime/Function.h"

#include "runtime/Array.h"
#include "runtime/String.h"
#include "runtime/HashTable.h"
#include "native/Operations.h"

ML_NAMESPACE_END;

#include "patch/MemoryPatch.h"
#include "search/Sunday.h"
#include "search/Automaton.h"
#include "search/Pattern.h"

#endif // CPP_DEFINED

#endif // _MYLIBRARYUSER_H_e5b0d13b_823a_4f24_88ec_6515f58a7140_

#if CPP_DEFINED
#endif

ML_NAMESPACE

LONG_PTR MlInitialize();
LONG_PTR MlUnInitialize();

ML_NAMESPACE_END;

#if CPP_DEFINED
#ifndef _MLNS_H_6cb317cc_3fcc_4a63_87ee_12bcfeb1db61_
#define _MLNS_H_6cb317cc_3fcc_4a63_87ee_12bcfeb1db61_

using namespace ml::Native;
using namespace ml::Native::Ldr;
using namespace ml::Native::Mm;
using namespace ml::Native::Ps;
using namespace ml::Native::Reg;
using namespace ml::Native::Rtl;
using namespace ml::Native::Nls;
using namespace ml::Native::Exp;
using namespace ml::Native::Lpc;
using namespace ml::Native::Io;

#endif // _MLNS_H_6cb317cc_3fcc_4a63_87ee_12bcfeb1db61_
#endif

#endif // _MYLIBRARY_H_9fb91137_38ac_4d5e_8572_b6e12d23cb15_
