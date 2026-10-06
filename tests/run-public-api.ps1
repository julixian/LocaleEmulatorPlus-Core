param(
    [string]$VcTools = 'D:\VisualStudio2026\VC\Tools\MSVC\14.44.35207',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $repoRoot 'out'
foreach ($arch in @('x86','x64')) {
    $compiler = "$VcTools\bin\Hostx64\$arch\cl.exe"
    $env:LIB = "$VcTools\lib\$arch;$SdkRoot\Lib\$SdkVersion\ucrt\$arch;$SdkRoot\Lib\$SdkVersion\um\$arch"
    $includes = @("/I$VcTools\include", "/I$SdkRoot\Include\$SdkVersion\ucrt", "/I$SdkRoot\Include\$SdkVersion\shared", "/I$SdkRoot\Include\$SdkVersion\um", "/I$SdkRoot\Include\$SdkVersion\km")
    $ifc = "$outputRoot\LocaleEmulatorPlus-$arch.ifc"
    $moduleObject = "$outputRoot\LocaleEmulatorPlus-module-$arch.obj"
    & $compiler /nologo /c /std:c++20 /EHsc /MT $includes /interface "$repoRoot\include\LocaleEmulatorPlus.ixx" "/ifcOutput$ifc" "/Fo$moduleObject"
    if ($LASTEXITCODE -ne 0) { throw "Module compile failed: $arch" }
    $archDefine = if ($arch -eq 'x64') { '/D_WIN64' } else { '/DWIN32' }
    $coreObject = "$outputRoot\lepb-core-layout-$arch.obj"
    & $compiler /nologo /c /O2 /EHsc /MT /D_NO_CRT_STDIO_INLINE /DML_DISABLE_THIRD_LIB=1 $archDefine $includes "$PSScriptRoot\lepb-core-layout.cpp" "/Fo$coreObject"
    if ($LASTEXITCODE -ne 0) { throw "Core layout compile failed: $arch" }
    & $compiler /nologo /std:c++20 /EHsc /MT $includes /reference "LocaleEmulatorPlus=$ifc" "$PSScriptRoot\lepb-public-layout.cpp" $coreObject $moduleObject "/Fo$outputRoot\lepb-public-layout-$arch.obj" "/Fe$outputRoot\lepb-public-layout-$arch.exe"
    if ($LASTEXITCODE -ne 0) { throw "Public layout compile failed: $arch" }
    & "$outputRoot\lepb-public-layout-$arch.exe"
    if ($LASTEXITCODE -ne 0) { throw "Public/Core layout mismatch: $arch" }
    & $compiler /nologo /std:c++20 /EHsc /MT $includes /reference "LocaleEmulatorPlus=$ifc" "$repoRoot\examples\lep-launch.cpp" $moduleObject "/Fo$outputRoot\lep-launch-$arch.obj" "/Fe$outputRoot\lep-launch-$arch.exe"
    if ($LASTEXITCODE -ne 0) { throw "Example compile failed: $arch" }
    $previousTestFlag = $env:LEP_PUBLIC_API_TEST
    try {
        $env:LEP_PUBLIC_API_TEST = '1'
        & "$outputRoot\lep-launch-$arch.exe" "$outputRoot\$arch\LoaderDll_$arch.dll" "$outputRoot\lepb-public-layout-$arch.exe"
        if ($LASTEXITCODE -ne 0) { throw "Public caller runtime failed: $arch" }
    } finally { $env:LEP_PUBLIC_API_TEST = $previousTestFlag }
    Write-Output "${arch}: module, Core ABI comparison and public caller passed"
}
