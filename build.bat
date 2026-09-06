@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"

if not defined VS_ROOT set "VS_ROOT=D:\VisualStudio2026"
if not defined MSVC_VER set "MSVC_VER=14.44.35207"
if not defined SDK_VER set "SDK_VER=10.0.26100.0"
set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=all"

if not defined EXTRA_CL set "EXTRA_CL=/DENABLE_LOG=1"

if /I "%TARGET%"=="all" (
  call "%~f0" x86 || exit /b 1
  call "%~f0" x64 || exit /b 1
  exit /b 0
)

if /I not "%TARGET%"=="x86" if /I not "%TARGET%"=="x64" if /I not "%TARGET%"=="all" (
  echo Usage: build.bat [x86^|x64^|all]
  exit /b 1
)

set "VC_TOOLS=%VS_ROOT%\VC\Tools\MSVC\%MSVC_VER%"
if /I "%TARGET%"=="x86" (
  set "HOST_BIN=%VC_TOOLS%\bin\Hostx64\x86"
  set "MACHINE=x86"
  set "SDK_ARCH=x86"
  set "VC_ARCH=x86"
  set "SUFFIX=x86"
  set "OUT=%ROOT%\out\x86"
  set "OBJ=%ROOT%\out\obj\x86"
  set "COMMON_ARCH_CL=/Gr /arch:SSE /DWIN32"
  set "SAFESEH=/safeseh:no"
  set "ALT_UNDOC=/alternatename:__imp__LdrInitializeThunk@8=__imp__LdrInitializeThunk /alternatename:_LdrInitializeThunk@8=_LdrInitializeThunk /alternatename:__imp__LdrRegisterDllNotification@16=__imp__LdrRegisterDllNotification /alternatename:_LdrRegisterDllNotification@16=_LdrRegisterDllNotification /alternatename:__imp__LdrUnregisterDllNotification@4=__imp__LdrUnregisterDllNotification /alternatename:_LdrUnregisterDllNotification@4=_LdrUnregisterDllNotification /alternatename:__imp__CreateProcessInternalW@48=__imp__CreateProcessInternalW /alternatename:_CreateProcessInternalW@48=_CreateProcessInternalW"
) else (
  set "HOST_BIN=%VC_TOOLS%\bin\Hostx64\x64"
  set "MACHINE=x64"
  set "SDK_ARCH=x64"
  set "VC_ARCH=x64"
  set "SUFFIX=x64"
  set "OUT=%ROOT%\out\x64"
  set "OBJ=%ROOT%\out\obj\x64"
  set "COMMON_ARCH_CL=/DWIN64 /D_WIN64"
  set "SAFESEH="
  set "ALT_UNDOC="
)

set "CL_EXE=%HOST_BIN%\cl.exe"
set "LIB=%HOST_BIN%\lib.exe"
set "DEP=%ROOT%\dep"
set "LINK_EXE=%DEP%\tools\link.exe"
set "PATH=%HOST_BIN%;%DEP%\tools;%PATH%"

set "SDK_INC=C:\Program Files (x86)\Windows Kits\10\Include\%SDK_VER%"
set "SDK_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\%SDK_VER%"

if not exist "%CL_EXE%" (
  echo cl.exe not found: "%CL_EXE%"
  exit /b 1
)
if not exist "%LINK_EXE%" (
  echo patched link.exe not found: "%LINK_EXE%"
  exit /b 1
)
if not exist "%SDK_INC%\um\winnt.h" (
  echo Windows SDK headers not found: "%SDK_INC%"
  exit /b 1
)

set "LIBOUT=%ROOT%\out\libs\%TARGET%"
set "LEP_DLL=LocaleEmulatorPlus_%SUFFIX%.dll"
set "LOADER_DLL=LoaderDll_%SUFFIX%.dll"
set "NTDLL_IMPORT_LIB=lep_ntdll_%SUFFIX%.lib"
set "K32_IMPORT_LIB=lep_k32_%SUFFIX%.lib"

if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%LIBOUT%" mkdir "%LIBOUT%"
if not exist "%OUT%" mkdir "%OUT%"

set "COMMON_INC=/I"%ROOT%\LocaleEmulatorPlus" /I"%VC_TOOLS%\include" /I"%SDK_INC%\ucrt" /I"%SDK_INC%\shared" /I"%SDK_INC%\um" /I"%SDK_INC%\km""
set "COMMON_CL=/nologo /c /O2 /Ob1 /GF /Gy /GR- /EHs-c- /GS- %COMMON_ARCH_CL% /DNDEBUG %EXTRA_CL% /D_NO_CRT_STDIO_INLINE /Zc:wchar_t"
set "LIBPATHS=/libpath:"%SDK_LIB%\um\%SDK_ARCH%" /libpath:"%SDK_LIB%\ucrt\%SDK_ARCH%" /libpath:"%VC_TOOLS%\lib\%VC_ARCH%""

echo [%TARGET% 1/5] Generating import libraries
if /I "%TARGET%"=="x86" (
  "%LIB%" /nologo /machine:%MACHINE% /def:"%DEP%\libs\lep_ntdll_x86.def" /out:"%LIBOUT%\%NTDLL_IMPORT_LIB%" || exit /b 1
  "%LIB%" /nologo /machine:%MACHINE% /def:"%DEP%\libs\lep_k32_x86.def" /out:"%LIBOUT%\%K32_IMPORT_LIB%" || exit /b 1
) else (
  "%LIB%" /nologo /machine:%MACHINE% /def:"%DEP%\libs\lep_ntdll_x64.def" /out:"%LIBOUT%\%NTDLL_IMPORT_LIB%" || exit /b 1
  "%LIB%" /nologo /machine:%MACHINE% /def:"%DEP%\libs\lep_k32_x64.def" /out:"%LIBOUT%\%K32_IMPORT_LIB%" || exit /b 1
)
"%LIB%" /nologo /machine:%MACHINE% /def:"%DEP%\libs\ntdll_vsnprintf_x86_x64.def" /out:"%LIBOUT%\ntdll_vsnprintf.lib" || exit /b 1

echo [%TARGET% 2/5] Building LEP delay-load helper
"%CL_EXE%" %COMMON_CL% /Fo"%OBJ%\LepDelayLoad.obj" %COMMON_INC% "%DEP%\libs\LepDelayLoad_x86_x64.cpp" || exit /b 1
if /I "%TARGET%"=="x64" (
  "%CL_EXE%" %COMMON_CL% /Oi- /Fo"%OBJ%\LepCrtShim.obj" %COMMON_INC% "%DEP%\libs\LepCrtShim_x64.cpp" || exit /b 1
  "%LIB%" /nologo /out:"%LIBOUT%\LepMyLib.lib" "%OBJ%\LepDelayLoad.obj" "%OBJ%\LepCrtShim.obj" || exit /b 1
) else (
  "%LIB%" /nologo /out:"%LIBOUT%\LepMyLib.lib" "%OBJ%\LepDelayLoad.obj" || exit /b 1
)

echo [%TARGET% 3/5] Compiling LocaleEmulatorPlus
pushd "%ROOT%\LocaleEmulatorPlus" || exit /b 1
for %%F in (
  HandleTable.cpp
  Hooks\Gdi32Hook.cpp
  Hooks\Kernel32Hook.cpp
  Hooks\NtdllHook.cpp
  Hooks\User32Hook.cpp
  LocaleEmulatorPlus.cpp
  ml.cpp
  stdafx.cpp
  Utility\Utility.cpp
) do (
  "%CL_EXE%" %COMMON_CL% /Fo"%OBJ%\LEP_%%~nF.obj" %COMMON_INC% "%%F" || exit /b 1
)
"%CL_EXE%" %COMMON_CL% /Fo"%OBJ%\LEP_HookPort.obj" %COMMON_INC% "HookPort.cpp" || exit /b 1
popd

echo [%TARGET% 4/5] Linking %LEP_DLL%
set "LEP_OBJECTS="%OBJ%\LEP_HandleTable.obj" "%OBJ%\LEP_Gdi32Hook.obj" "%OBJ%\LEP_Kernel32Hook.obj" "%OBJ%\LEP_NtdllHook.obj" "%OBJ%\LEP_User32Hook.obj" "%OBJ%\LEP_LocaleEmulatorPlus.obj" "%OBJ%\LEP_ml.obj" "%OBJ%\LEP_stdafx.obj" "%OBJ%\LEP_Utility.obj""
set "LEP_OBJECTS=%LEP_OBJECTS% "%OBJ%\LEP_HookPort.obj""

"%LINK_EXE%" /nologo /dll /out:"%OUT%\%LEP_DLL%" /implib:"%OUT%\LocaleEmulatorPlus_%SUFFIX%.lib" ^
  %LEP_OBJECTS% ^
  %LIBPATHS% /nodefaultlib /debug:none /opt:ref /ignore:4254 %SAFESEH% /manifest:no /dynamicbase:no /nxcompat /machine:%MACHINE% /entry:DllMain /subsystem:windows ^
  /export:LoadFirstDll ^
  /delayload:KERNEL32.dll /delayload:USER32.dll /delayload:GDI32.dll /delayload:DBGHELP.dll ^
  %ALT_UNDOC% ^
  "%LIBOUT%\LepMyLib.lib" ntdll.lib "%LIBOUT%\%NTDLL_IMPORT_LIB%" "%LIBOUT%\%K32_IMPORT_LIB%" "%LIBOUT%\ntdll_vsnprintf.lib" kernel32.lib user32.lib gdi32.lib dbghelp.lib libcmt.lib oldnames.lib libvcruntime.lib || exit /b 1

echo [%TARGET% 5/5] Building %LOADER_DLL%
pushd "%ROOT%" || exit /b 1
"%CL_EXE%" %COMMON_CL% /DLEP_LOADER_DLL=1 /Fo"%OBJ%\LoaderDll.obj" %COMMON_INC% "%ROOT%\LoaderDll\LoaderDll.cpp" || exit /b 1
popd

"%LINK_EXE%" /nologo /dll /noentry /out:"%OUT%\%LOADER_DLL%" /implib:"%OUT%\LoaderDll_%SUFFIX%.lib" /def:"%ROOT%\LoaderDll\LoaderDll.def" ^
  "%OBJ%\LoaderDll.obj" ^
  %LIBPATHS% /nodefaultlib /debug:none /opt:ref /ignore:4254 %SAFESEH% /manifest:no /machine:%MACHINE% /subsystem:windows ^
  %ALT_UNDOC% ^
  "%LIBOUT%\LepMyLib.lib" ntdll.lib "%LIBOUT%\%NTDLL_IMPORT_LIB%" "%LIBOUT%\%K32_IMPORT_LIB%" "%LIBOUT%\ntdll_vsnprintf.lib" kernel32.lib gdi32.lib dbghelp.lib libcmt.lib oldnames.lib libvcruntime.lib || exit /b 1

echo.
echo [%TARGET%] Build succeeded.
echo   "%OUT%\%LEP_DLL%"
echo   "%OUT%\%LOADER_DLL%"
exit /b 0
