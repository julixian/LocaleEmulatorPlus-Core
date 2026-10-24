#pragma once

// Internal umbrella. Public callers use include/LocaleEmulatorPlus.ixx.
#include "base/Platform.h"
#include "runtime/MemoryPrimitives.h"
#include "base/Pointer.h"
#include "pe/ExportHash.h"
#include "native/Helpers.h"
#include "runtime/ActiveFrame.h"
#include "runtime/Heap.h"
#include "runtime/Diagnostics.h"
#include "pe/Image.h"
#include "runtime/File.h"
#include "patch/Decoder.h"

#include "runtime/TypeTraits.h"
#include "runtime/Array.h"
#include "runtime/String.h"
#include "runtime/HashTable.h"
#include "native/Operations.h"

#include "patch/MemoryPatch.h"
#include "search/Sunday.h"
#include "search/Pattern.h"

using namespace lep::Native;
using namespace lep::Native::Ldr;
using namespace lep::Native::Mm;
using namespace lep::Native::Ps;
using namespace lep::Native::Reg;
using namespace lep::Native::Rtl;
using namespace lep::Native::Nls;
using namespace lep::Native::Exp;
using namespace lep::Native::Io;
