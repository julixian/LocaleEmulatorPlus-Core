#pragma once
#include "../native/Helpers.h"

#ifndef _LDASM_H_6c4dff54_0f1d_4946_b50c_f9b96c5ade1e_
#define _LDASM_H_6c4dff54_0f1d_4946_b50c_f9b96c5ade1e_


ULONG_PTR FASTCALL LdeGetOpCodeSize32(PVOID Code, PVOID DEF_VAL(*OpCodePtr, NULL));
ULONG_PTR FASTCALL LdeGetOpCodeSize64(PVOID Code, PVOID DEF_VAL(*OpCodePtr, NULL));
ULONG_PTR FASTCALL SizeOfProc(PVOID Proc);
BOOL FASTCALL IsRelativeCmd(PBYTE OpCode);


ForceInline ULONG_PTR GetOpCodeSize32(PVOID Buffer)
{
    return LdeGetOpCodeSize32(Buffer);
}

ForceInline ULONG_PTR GetOpCodeSize64(PVOID Buffer)
{
    return LdeGetOpCodeSize64(Buffer);
}

ForceInline ULONG_PTR GetOpCodeSize(PVOID Buffer)
{
#if ML_AMD64
    return GetOpCodeSize64(Buffer);
#elif ML_X86
    return GetOpCodeSize32(Buffer);
#endif
}

#define WalkOpCodeM(Buffer, OpLength, Ret) \
            [&] (PBYTE Buffer, ULONG_PTR OpLength, PVOID &Ret) -> NTSTATUS

template<typename T>
PVOID WalkOpCode64T(PVOID Buffer, LONG_PTR Size, T Callback)
{
    PVOID ret = nullptr;

    PBYTE _Buffer = (PBYTE)Buffer;

    for (; Size > 0; )
    {
        ULONG_PTR Length;

        Length = GetOpCodeSize64(_Buffer);

        if (NT_SUCCESS(Callback(_Buffer, Length, ret)))
            break;

        _Buffer += Length;
        Size -= Length;
    }

    return ret;
}

template<typename T>
PVOID WalkOpCodeT(PVOID Buffer, LONG_PTR Size, T Callback)
{
    PVOID ret = nullptr;

    PBYTE _Buffer = (PBYTE)Buffer;

    for (; Size > 0; )
    {
        ULONG_PTR Length;

        Length = GetOpCodeSize(_Buffer);

        if (NT_SUCCESS(Callback(_Buffer, Length, ret)))
            break;

        _Buffer += Length;
        Size -= Length;
    }

    return ret;
}


#endif // _LDASM_H_6c4dff54_0f1d_4946_b50c_f9b96c5ade1e_
