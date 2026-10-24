#pragma once

#include "../native/Declarations.h"
#include <stddef.h>

namespace lep
{
    // The Native prefix follows the Windows layout. LEP's tag, payload and
    // lifetime bookkeeping are outside that prefix.
    struct ActiveFrame
    {
        TEB_ACTIVE_FRAME Native;
        ULONG Size;
        ULONG Version;
        ULONG Kind;
        ULONG_PTR Data;
        BOOL NeedPop;

        explicit ActiveFrame(ULONG Kind = 0)
            : Native{}, Size(sizeof(ActiveFrame)), Version(1), Kind(Kind),
              Data(0), NeedPop(FALSE)
        {
            Native.Context = Description();
        }

        ActiveFrame(const ActiveFrame&) = delete;
        ActiveFrame& operator=(const ActiveFrame&) = delete;
        ActiveFrame(ActiveFrame&&) = delete;
        ActiveFrame& operator=(ActiveFrame&&) = delete;

        ~ActiveFrame()
        {
            Pop();
        }

        VOID Push()
        {
            if (NeedPop)
                return;
            RtlPushFrame(&Native);
            NeedPop = TRUE;
        }

        VOID Pop()
        {
            if (!NeedPop)
                return;
            NeedPop = FALSE;
            RtlPopFrame(&Native);
        }

        static ActiveFrame* Find(ULONG_PTR Kind)
        {
            for (auto NativeFrame = RtlGetFrame(); NativeFrame != nullptr;
                 NativeFrame = NativeFrame->Previous)
            {
                // Check the Native descriptor before reading LEP's extension.
                if (NativeFrame->Flags != 0 || NativeFrame->Context == nullptr ||
                    NativeFrame->Context->Flags != 0 ||
                    !SameName(NativeFrame->Context->FrameName, Description()->FrameName))
                    continue;

                auto Frame = reinterpret_cast<ActiveFrame*>(NativeFrame);
                if (Frame->Size == sizeof(ActiveFrame) && Frame->Version == 1 && Frame->Kind == Kind)
                    return Frame;
            }
            return nullptr;
        }

    private:
        static PTEB_ACTIVE_FRAME_CONTEXT Description()
        {
            // Constant initialization; no CRT startup or dynamic TLS. Each
            // module may have its own descriptor, so Find compares the name.
            static TEB_ACTIVE_FRAME_CONTEXT Value = {0, "LocaleEmulatorPlus.ActiveFrame.v1"};
            return &Value;
        }

        static BOOL SameName(PCSTR Left, PCSTR Right)
        {
            if (Left == nullptr)
                return FALSE;
            while (*Left == *Right)
            {
                if (*Right == 0)
                    return TRUE;
                ++Left;
                ++Right;
            }
            return FALSE;
        }
    };

    static_assert(sizeof(TEB_ACTIVE_FRAME) == (sizeof(PVOID) == 8 ? 24 : 12), "Native active-frame size");
    static_assert(offsetof(TEB_ACTIVE_FRAME, Previous) == sizeof(PVOID), "Native previous-frame offset");
    static_assert(offsetof(TEB_ACTIVE_FRAME, Context) == 2 * sizeof(PVOID), "Native frame-context offset");
    static_assert(offsetof(ActiveFrame, Native) == 0, "Native frame must be the first member");
    static_assert(offsetof(ActiveFrame, Size) == sizeof(TEB_ACTIVE_FRAME), "LEP data must follow the Native frame");
}
