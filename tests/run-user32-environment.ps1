param(
    [string]$VcTools = 'D:\VisualStudio2026\VC\Tools\MSVC\14.44.35207',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
foreach ($arch in @('x86','x64')) {
    $output = "$root\out\user32-environment-tests\$arch"
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    $env:LIB = "$VcTools\lib\$arch;$SdkRoot\Lib\$SdkVersion\ucrt\$arch;$SdkRoot\Lib\$SdkVersion\um\$arch"
    $includes = @("/I$root\LocaleEmulatorPlus", "/I$VcTools\include", "/I$SdkRoot\Include\$SdkVersion\ucrt", "/I$SdkRoot\Include\$SdkVersion\shared", "/I$SdkRoot\Include\$SdkVersion\um", "/I$SdkRoot\Include\$SdkVersion\km")
    $flags = @('/nologo','/utf-8','/MT','/EHsc','/O2','/DENABLE_LOG=0')
    $linkFlags = @()
    if ($arch -eq 'x86') { $flags += '/Gr' }
    if ($arch -eq 'x86') { $linkFlags += '/alternatename:__imp__CreateProcessInternalW@48=__imp__CreateProcessInternalW' }
    & "$VcTools\bin\Hostx64\$arch\cl.exe" $flags $includes "$PSScriptRoot\user32-environment-probe.cpp" "$root\LocaleEmulatorPlus\Hooks\User32Environment.cpp" "$root\out\libs\$arch\LepSupport.lib" "$root\out\libs\$arch\lep_k32_$arch.lib" kernel32.lib user32.lib ntdll.lib "/Fo$output\" "/Fe$output\probe.exe" /link $linkFlags
    if ($LASTEXITCODE -ne 0) { throw "Environment unit compile failed: $arch" }
    & "$output\probe.exe" 2>&1 | Tee-Object "$output\results.log"
    if ($LASTEXITCODE -ne 0) { throw "Environment unit failed: $arch exit=$LASTEXITCODE" }
}
