#include "../Support.h"
_ML_C_HEAD_


#pragma comment(lib, "dbghelp.lib")
#include <DbgHelp.h>

static PWSTR AppendHex64NoLeading(PWSTR Buffer, ULONG64 Value)
{
    static const WCHAR Hex[] = L"0123456789ABCDEF";
    BOOLEAN Started = FALSE;

    for (LONG_PTR Shift = 60; Shift >= 0; Shift -= 4)
    {
        ULONG Digit = (ULONG)((Value >> Shift) & 0xF);

        if (Digit != 0 || Started || Shift == 0)
        {
            *Buffer++ = Hex[Digit];
            Started = TRUE;
        }
    }

    return Buffer;
}

NTSTATUS CreateMiniDump(PEXCEPTION_POINTERS ExceptionPointers)
{
    WCHAR           MiniDumpFile[MAX_NTPATH];
    PWSTR           MiniDumpExt;
    NtFileDisk      File;
    BOOL            Success;
    NTSTATUS        Status;
    PROCESS_TIMES   Times;

    MINIDUMP_EXCEPTION_INFORMATION ExceptionInformation;

    NtQueryInformationProcess(NtCurrentProcess(), ProcessTimes, &Times, sizeof(Times), NULL);

    Nt_GetModuleFileName(NULL, MiniDumpFile, countof(MiniDumpFile));
    MiniDumpExt = findextw(MiniDumpFile);
    *MiniDumpExt++ = L'.';
    MiniDumpExt = AppendHex64NoLeading(MiniDumpExt, (ULONG64)Times.CreationTime.QuadPart);
    CopyMemory(MiniDumpExt, L".crash.dmp", sizeof(L".crash.dmp"));

    Status = File.Create(MiniDumpFile);
    FAIL_RETURN(Status);

    ExceptionInformation.ClientPointers     = FALSE;
    ExceptionInformation.ExceptionPointers  = ExceptionPointers;
    ExceptionInformation.ThreadId           = Ps::CurrentTid();

    Success = MiniDumpWriteDump(
                NtCurrentProcess(),
                (ULONG_PTR)Ps::CurrentTeb()->ClientId.UniqueProcess,
                File,
                MiniDumpNormal,
                &ExceptionInformation,
                NULL,
                NULL
              );

    return Success ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

/************************************************************************
  directory
************************************************************************/
BOOL IsPathExistsW(LPCWSTR pszPath)
{
    if (GetFileAttributesW(pszPath) != -1)
        return TRUE;

    switch (GetLastError())
    {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_NAME:
            return FALSE;
    }

    return TRUE;
}

BOOL IsPathExistsA(LPCSTR pszPath)
{
    WCHAR szPath[MAX_PATH];

    MultiByteToWideChar(CP_ACP, 0, pszPath, -1, szPath, countof(szPath));

    return IsPathExistsW(szPath);
}

BOOL CreateDirectoryWorker(LPCWSTR PathName)
{
    NTSTATUS    Status;
    HANDLE      DirectoryHandle;

    Status = NtFileDisk::CreateDirectory(&DirectoryHandle, PathName);
    if (!NT_SUCCESS(Status))
        return FALSE;

    NtClose(DirectoryHandle);
    return TRUE;

}

BOOL CreateDirectoryRecursiveW(LPCWSTR lpPathName)
{
    WCHAR  ch;
    LPWSTR lpPath = (LPWSTR)lpPathName;

    if (lpPath == NULL || *lpPath == 0)
        return FALSE;

    if (Nt_IsPathExists(lpPathName))
        return TRUE;

    do
    {
        if (*lpPath == '\\' || *lpPath == '/')
        {
            if (*(lpPath + 1) == 0)
            {
                break;
            }
            ch = *lpPath;
            *lpPath = 0;
            CreateDirectoryWorker(lpPathName);
            *lpPath = ch;
        }
        ++lpPath;
    } while (*lpPath);

    return CreateDirectoryWorker(lpPathName);
}

BOOL FASTCALL MyCreateDirectoryA(LPCSTR lpPathName)
{
    WCHAR szPath[MAX_PATH * 2];

    MultiByteToWideChar(CP_ACP, 0, lpPathName, -1, szPath, countof(szPath));
    return CreateDirectoryRecursiveW(szPath);
}

ULONG GetExeDirectoryW(LPWSTR pszPath, ULONG BufferCount)
{
    WCHAR szPath[MAX_PATH];
    ULONG i;

    if (pszPath == NULL)
    {
        pszPath = szPath;
        BufferCount = countof(szPath);
    }

    i = GetModuleFileNameW(NULL, pszPath, BufferCount);
    if (i)
    {
        while (pszPath[--i] != '\\');
        pszPath[++i] = 0;
    }

    return i;
}

ULONG GetExeDirectoryA(LPSTR pszPath, ULONG BufferCount)
{
    WCHAR  szPath[MAX_PATH];
    ULONG i;

    i = GetExeDirectoryW(szPath, countof(szPath));
    if (i)
    {
        i = WideCharToMultiByte(CP_ACP, 0, szPath, i + 1, pszPath, BufferCount, NULL, NULL) - 1;
    }

    return i;
}


inline BOOL __IsParentDirectory(LPWSTR pszPath)
{
    if (*(PULONG)pszPath == '.')
        return TRUE;
    else if ((*(PULONG64)pszPath & 0x0000FFFFFFFFFFFF) == TAG2W('..'))
        return TRUE;

    return FALSE;
}

LONG ProcessFindResult(ENUM_DIRECTORY_INFO *pFindInfo, LPWSTR lpPath, LPWSTR lpFileName)
{
    LONG  Result;
    PVOID lpBuffer;

    StrCopyW(lpFileName, pFindInfo->wfd.cFileName);
    StrCopyW(pFindInfo->wfd.cFileName, lpPath);

    lpBuffer = pFindInfo->lpOutBuffer;
    if (lpBuffer != NULL)
    {
        if (pFindInfo->ElemCount.QuadPart >= pFindInfo->MaxCount.QuadPart)
        {
            pFindInfo->MaxCount.QuadPart *= 2;
            lpBuffer = RtlReAllocateHeap(
                            pFindInfo->hHeap,
                            HEAP_ZERO_MEMORY,
                            pFindInfo->lpBuffer,
                            (ULONG_PTR)pFindInfo->MaxCount.QuadPart * pFindInfo->ElemSize
                       );

            if (lpBuffer == NULL)
                return -1;

            pFindInfo->lpBuffer = lpBuffer;
            lpBuffer = (PBYTE)lpBuffer + pFindInfo->ElemCount.QuadPart * pFindInfo->ElemSize;
            pFindInfo->lpOutBuffer = lpBuffer;
        }

        lpBuffer = pFindInfo->lpOutBuffer;
    }

    Result = pFindInfo->CallBack(lpBuffer, &pFindInfo->wfd, pFindInfo->Context);
    if (Result != 0)
        pFindInfo->ElemCount.QuadPart += Result;

    if (lpBuffer != NULL)
        pFindInfo->lpOutBuffer = (PBYTE)lpBuffer + pFindInfo->ElemSize;

    return Result;
}

IF_NOT_EXIST(_wfinddata64_t)
{
    typedef struct
    {
        unsigned    attrib;
        __int64  time_create;    /* -1 for FAT file systems */
        __int64  time_access;    /* -1 for FAT file systems */
        __int64  time_write;
        __int64     size;
        wchar_t     name[260];
    } _wfinddata64_t;
}

VOID CopyCrtFindDataToWin32FindData(PWIN32_FIND_DATAW FindFileData, _wfinddata64_t *finddata)
{
    LARGE_INTEGER  LargeInt;
    ULARGE_INTEGER LargeUInt;

    FindFileData->dwFileAttributes                  = finddata->attrib;

    LargeInt.QuadPart                               = finddata->size;
    FindFileData->nFileSizeLow                      = LargeInt.LowPart;
    FindFileData->nFileSizeHigh                     = LargeInt.HighPart;

    LargeUInt.QuadPart                              = finddata->time_create;
    FindFileData->ftCreationTime.dwLowDateTime      = LargeUInt.LowPart;
    FindFileData->ftCreationTime.dwHighDateTime     = LargeUInt.HighPart;

    LargeUInt.QuadPart                              = finddata->time_access;
    FindFileData->ftLastAccessTime.dwLowDateTime    = LargeUInt.LowPart;
    FindFileData->ftLastAccessTime.dwHighDateTime   = LargeUInt.HighPart;

    LargeUInt.QuadPart                              = finddata->time_write;
    FindFileData->ftLastWriteTime.dwLowDateTime     = LargeUInt.LowPart;
    FindFileData->ftLastWriteTime.dwHighDateTime    = LargeUInt.HighPart;

    FindFileData->cAlternateFileName[0]             = 0;
    CopyStruct(FindFileData->cFileName, finddata->name, MY_MIN(sizeof(finddata->name), sizeof(FindFileData->cFileName)));
}

HANDLE FindFirstFileWorker(LPCWSTR FileName, PWIN32_FIND_DATAW FindFileData)
{
    return Nt_FindFirstFile(FileName, FindFileData);
}

BOOL FindNextFileWorker(HANDLE FindFileHandle, PWIN32_FIND_DATAW FindFileData)
{
    return Nt_FindNextFile(FindFileHandle, FindFileData);
}

BOOL FindCloseWorker(HANDLE FindFileHandle)
{
    return Nt_FindClose(FindFileHandle);
}

BOOL IsCircleSymbolicLink(ENUM_DIRECTORY_INFO *pFindInfo, LPWSTR lpPath, LPWSTR lpFileName)
{
    if (!FLAG_ON(pFindInfo->wfd.dwFileAttributes, FILE_ATTRIBUTE_REPARSE_POINT))
        return FALSE;

    ULONG_PTR   Length, CurrentPathLength;
    PWCHAR      ShorterPath, LinkedPath;
    HANDLE      SymbolicLink;
    NTSTATUS    Status;

    StrCopyW(lpFileName, pFindInfo->wfd.cFileName);
    if (!NT_SUCCESS(NtFileDisk::OpenSymbolicLink(&SymbolicLink, lpPath)))
        return FALSE;

    LinkedPath  = pFindInfo->SymbolicLinkPath;
    Length      = countof(pFindInfo->SymbolicLinkPath);
    Status      = NtFileDisk::QuerySymbolicTarget(SymbolicLink, LinkedPath, &Length);

    NtFileDisk::Close(SymbolicLink);
    if (!NT_SUCCESS(Status))
        return FALSE;

    if (LinkedPath[Length - 1] != '\\')
        LinkedPath[Length++] = '\\';

    CurrentPathLength = StrLengthW(lpPath);
    if (CurrentPathLength < Length)
    {
        ShorterPath = lpPath;
        Length = CurrentPathLength;
    }
    else
    {
        ShorterPath = LinkedPath;
    }

    if (lpPath[Length - 1] != '\\' || LinkedPath[Length - 1] != '\\')
        return FALSE;

    return !StrNICompareW(lpPath, LinkedPath, Length);
}

BOOL EnumDirectoryFilesWorker(LPWSTR lpPath, ENUM_DIRECTORY_INFO *pFindInfo)
{
    BOOL    bIsDirectory;
    LPWSTR  lpEnd, lpFileName;
    HANDLE  hFind;
    LONG    Result;

    lpEnd = lpPath + StrLengthW(lpPath);
    if (pFindInfo->wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        if (*(lpEnd - 1) != '\\')
            *lpEnd++ = '\\';

        lpFileName = lpEnd;
        StrCopyW(lpEnd, pFindInfo->pszFilter);
        bIsDirectory = TRUE;
    }
    else
    {
        lpFileName   = findnamew(lpPath);
        bIsDirectory = FALSE;
    }

    hFind = FindFirstFileWorker(lpPath, &pFindInfo->wfd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (FLAG_ON(pFindInfo->wfd.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY))
            {
                if (__IsParentDirectory(pFindInfo->wfd.cFileName))
                    continue;

                if (IsCircleSymbolicLink(pFindInfo, lpPath, lpFileName))
                    continue;

                if (FLAG_ON(pFindInfo->Flags, EDF_BEFORE))
                {
                    Result = ProcessFindResult(pFindInfo, lpPath, lpFileName);
                    if (Result == -1)
                    {
                        FindCloseWorker(hFind);
                        return FALSE;
                    }
                }

                if (!FLAG_ON(pFindInfo->Flags, EDF_SUBDIR))
                    continue;

                if (!FLAG_ON(pFindInfo->Flags, EDF_BEFORE))
                    StrCopyW(lpFileName, pFindInfo->wfd.cFileName);

                if (!EnumDirectoryFilesWorker(lpPath, pFindInfo))
                    break;
            }
            else
            {
                Result = ProcessFindResult(pFindInfo, lpPath, lpFileName);
                if (Result == -1)
                {
                    FindCloseWorker(hFind);
                    return FALSE;
                }
            }
        } while (FindNextFileWorker(hFind, &pFindInfo->wfd));

        FindCloseWorker(hFind);
    }

    if (bIsDirectory && FLAG_ON(pFindInfo->Flags, EDF_AFTER))
    {
        pFindInfo->wfd.cFileName[0] = 0;
        SET_FLAG(pFindInfo->wfd.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY);
        Result = ProcessFindResult(pFindInfo, lpPath, lpFileName);
        if (Result == -1)
            return FALSE;
    }

    *lpEnd = 0;

    return TRUE;
}

BOOL EnumDirectoryFilesFree(PVOID lpBuffer)
{
    return MemoryAllocator::FreeMemory(lpBuffer, 0);
}

BOOL
EnumDirectoryFiles(
    PVOID                      *lpFilesBuffer,
    LPCWSTR                     pszFilter,
    ULONG                       ElemSize,
    LPCWSTR                     pszPath,
    PLARGE_INTEGER              pElemCount,
    FEnumDirectoryFilesCallBack CallBack,
    ULONG_PTR                   Context,
    ULONG                       Flags
)
{
    WCHAR szPath[MAX_NTPATH];
    ENUM_DIRECTORY_INFO info;
    HANDLE hFind;

    if (CallBack == NULL)
        return FALSE;

    if (pElemCount != NULL)
        pElemCount->QuadPart = 0;

    if (pszPath != NULL)
    {
        hFind = FindFirstFileWorker(pszPath, &info.wfd);
        if (hFind == INVALID_HANDLE_VALUE)
            return FALSE;

        FindCloseWorker(hFind);
    }
    else
    {
        info.wfd.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    }

    info.ElemSize = ElemSize;
    info.MaxCount.QuadPart = 100;
    info.hHeap = MemoryAllocator::GetGlobalHeap();
    if (lpFilesBuffer != NULL)
    {
        info.lpBuffer = RtlAllocateHeap(info.hHeap, HEAP_ZERO_MEMORY, (ULONG_PTR)info.MaxCount.QuadPart * info.ElemSize);
        if (info.lpBuffer == NULL)
            return FALSE;
    }
    else
    {
        info.lpBuffer = NULL;
    }

    info.lpOutBuffer = info.lpBuffer;
    info.CallBack = CallBack;
    info.Context = Context;
    info.ElemCount.QuadPart = 0;
    info.pszFilter = pszFilter;
    info.Flags = Flags;

    if (pszPath != NULL)
    {
        StrCopyW(szPath, pszPath);
    }
    else
    {
        *(PULONG)szPath = L'.';
    }

    if (!EnumDirectoryFilesWorker(szPath, &info))
    {
        EnumDirectoryFilesFree(info.lpBuffer);
        return FALSE;
    }

    if (pElemCount != NULL)
        *pElemCount = info.ElemCount;
    if (lpFilesBuffer != NULL)
        *lpFilesBuffer = info.lpBuffer;

    return TRUE;
}

/************************************************************************
  command line
************************************************************************/

LONG_PTR FASTCALL CmdLineToArgvWorkerA(LPSTR pszCmdLine, LPSTR *pArgv, PLONG_PTR pCmdLineLength)
{
    LONG_PTR  argc;
    Char separator, ch;
    LPSTR pCmdLine;

    argc = 0;
    pCmdLine = pszCmdLine;

    LOOP_ALWAYS
    {
        separator = *pszCmdLine++;
        if (separator == 0)
            break;
        if (separator != '"')
            separator = ' ';

        ++argc;
        if (pArgv)
            *pArgv++ = (LPSTR)pszCmdLine - 1 + (separator == '"');

        LOOP_ALWAYS
        {
            ch = *pszCmdLine;
            if (ch == separator)
            {
                if (pArgv)
                    *pszCmdLine = 0;
                ++pszCmdLine;
                break;
            }
            else if (ch == 0)
                break;

            ++pszCmdLine;
        }
/*
        if (pArgv)
            *(pszCmdLine - 1)= 0;
*/
        while (*pszCmdLine == '\t' || *pszCmdLine == ' ')
            ++pszCmdLine;
    }

    if (pCmdLineLength)
        *pCmdLineLength = pszCmdLine - pCmdLine + 0;

    return argc;
}

LPSTR* FASTCALL CmdLineToArgvA(LPSTR pszCmdLine, PLONG_PTR pArgc)
{
    LONG_PTR argc, CmdLineLength;
    LPSTR *argv;

    argc = CmdLineToArgvWorkerA(pszCmdLine, NULL, &CmdLineLength);
    argv = (LPSTR *)AllocateMemory(CmdLineLength * sizeof(*pszCmdLine) + (argc + 1) * 4);
    if (argv == NULL)
        return NULL;

    CopyMemory(&argv[argc + 1], pszCmdLine, CmdLineLength * sizeof(*pszCmdLine));
    argv[argc] = 0;
    pszCmdLine = (LPSTR)&argv[argc + 1];

    argc = CmdLineToArgvWorkerA(pszCmdLine, argv, NULL);
    if (pArgc)
        *pArgc = argc;

    return (LPSTR *)argv;
}

LONG_PTR FASTCALL CmdLineToArgvWorkerW(LPWSTR pszCmdLine, LPWSTR *pArgv, PLONG_PTR pCmdLineLength)
{
    LONG_PTR  argc;
    WCHAR separator, ch;
    LPWSTR pCmdLine;

    argc = 0;
    pCmdLine = pszCmdLine;

    LOOP_ALWAYS
    {
        separator = *pszCmdLine++;
        if (separator == 0)
            break;
        if (separator != '"')
            separator = ' ';

        ++argc;
        if (pArgv)
            *pArgv++ = (LPWSTR)pszCmdLine - 1 + (separator == '"');

        LOOP_ALWAYS
        {
            ch = *pszCmdLine;
            if (ch == separator)
            {
                if (pArgv)
                    *pszCmdLine = 0;
                ++pszCmdLine;
                break;
            }
            else if (ch == 0)
                break;

            ++pszCmdLine;
        }
/*
        if (pArgv && ch != 0)
            *(pszCmdLine - 1)= 0;
*/
        while (*pszCmdLine == '\t' || *pszCmdLine == ' ')
            ++pszCmdLine;
    }

    if (pCmdLineLength)
        *pCmdLineLength = pszCmdLine - pCmdLine + 0;

    return argc;
}

LPWSTR* FASTCALL CmdLineToArgvW(LPWSTR pszCmdLine, PLONG_PTR pArgc)
{
    LONG_PTR argc, CmdLineLength;
    LPWSTR *argv;

    argc = CmdLineToArgvWorkerW(pszCmdLine, NULL, &CmdLineLength);
    argv = (LPWSTR *)AllocateMemory(CmdLineLength * sizeof(*pszCmdLine) + (argc + 1) * 4);
    if (argv == NULL)
        return NULL;

    CopyMemory(&argv[argc + 1], pszCmdLine, CmdLineLength * sizeof(*pszCmdLine));
    argv[argc] = 0;
    pszCmdLine = (LPWSTR)&argv[argc + 1];

    argc = CmdLineToArgvWorkerW(pszCmdLine, argv, NULL);
    if (pArgc)
        *pArgc = argc;

    return (LPWSTR *)argv;
}


_ML_C_TAIL_
