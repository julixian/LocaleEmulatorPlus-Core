#include "../LocaleEmulatorPlus/support/runtime/ActiveFrame.h"

extern "C" ULONG_PTR WINAPI ProbeFindFrameData(ULONG Kind)
{
    auto Frame = lep::ActiveFrame::Find(Kind);
    return Frame == nullptr ? 0 : Frame->Data;
}

extern "C" PTEB_ACTIVE_FRAME_CONTEXT WINAPI ProbeDescriptor()
{
    lep::ActiveFrame Frame;
    return Frame.Native.Context;
}

extern "C" BOOL WINAPI ProbeWithFrame(ULONG Kind, ULONG_PTR Data, BOOL (WINAPI* Callback)(ULONG, ULONG_PTR))
{
    lep::ActiveFrame Frame(Kind);
    Frame.Data = Data;
    Frame.Push();
    return Callback(Kind, Data);
}
