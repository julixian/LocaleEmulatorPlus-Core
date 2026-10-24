param(
    [string]$VcTools = 'D:\VisualStudio2026\VC\Tools\MSVC\14.44.35207',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $repoRoot 'out\active-frame-tests'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
foreach ($arch in @('x86','x64')) {
    $compiler = "$VcTools\bin\Hostx64\$arch\cl.exe"
    $env:LIB = "$VcTools\lib\$arch;$SdkRoot\Lib\$SdkVersion\ucrt\$arch;$SdkRoot\Lib\$SdkVersion\um\$arch"
    $includes = @("/I$VcTools\include", "/I$SdkRoot\Include\$SdkVersion\ucrt", "/I$SdkRoot\Include\$SdkVersion\shared", "/I$SdkRoot\Include\$SdkVersion\um", "/I$SdkRoot\Include\$SdkVersion\km")
    $definition = "$outputRoot\module-$arch.def"
    $exports = if ($arch -eq 'x86') {
        "EXPORTS`nProbeFindFrameData=_ProbeFindFrameData@4`nProbeDescriptor=_ProbeDescriptor@0`nProbeWithFrame=_ProbeWithFrame@12`n"
    } else { "EXPORTS`nProbeFindFrameData`nProbeDescriptor`nProbeWithFrame`n" }
    [IO.File]::WriteAllText($definition, $exports)
    $module = "$outputRoot\module-$arch.dll"
    & $compiler /nologo /utf-8 /LD /MT /EHsc /O2 $includes "$PSScriptRoot\active-frame-module.cpp" "/Fo$outputRoot\module-$arch.obj" "/Fe$module" ntdll.lib /link "/def:$definition" "/implib:$outputRoot\module-$arch.lib"
    if ($LASTEXITCODE -ne 0) { throw "Frame module compile failed: $arch" }
    $probe = "$outputRoot\probe-$arch.exe"
    & $compiler /nologo /utf-8 /MT /EHsc /O2 /D_NO_CRT_STDIO_INLINE $includes "$PSScriptRoot\active-frame-probe.cpp" "/Fo$outputRoot\probe-$arch.obj" "/Fe$probe" ntdll.lib user32.lib legacy_stdio_definitions.lib
    if ($LASTEXITCODE -ne 0) { throw "Frame probe compile failed: $arch" }
    & $probe $module
    if ($LASTEXITCODE -ne 0) { throw "Frame probe failed: $arch exit=$LASTEXITCODE" }
    foreach ($export in @('LepCreateProcess2','LepCreateProcess')) {
        & $probe --launch-window "$repoRoot\out\$arch\LoaderDll_$arch.dll" $export
        if ($LASTEXITCODE -ne 0) { throw "Window frame probe failed: $arch/$export exit=$LASTEXITCODE" }
    }
}
