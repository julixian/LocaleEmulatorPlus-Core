#pragma once

#include "../LocaleEmulatorPlus/LocaleEmulatorPlus.h"

// Input is read-only and may use the v2, v3 or v4 public ABI.
NTSTATUS LepResolveEnvironment(const void* Input, PLEP_RUNTIME_ENVIRONMENT Output);
