param(
    [ValidateSet('all','x86','x64')][string]$Architecture = 'all',
    [string]$VcTools = 'D:\VisualStudio2026\VC\Tools\MSVC\14.44.35207',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$arches = if ($Architecture -eq 'all') { @('x86', 'x64') } else { @($Architecture) }

foreach ($arch in $arches) {
    $out = "$root\out\locator\$arch"
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    $compiler = "$VcTools\bin\Hostx64\$arch\cl.exe"
    $env:PATH = "$SdkRoot\bin\$SdkVersion\x64;$VcTools\bin\Hostx64\$arch;$env:PATH"
    $env:LIB = "$VcTools\lib\$arch;$SdkRoot\Lib\$SdkVersion\ucrt\$arch;$SdkRoot\Lib\$SdkVersion\um\$arch"
    $includes = @(
        "/I$root\LocaleEmulatorPlus",
        "/I$VcTools\include",
        "/I$SdkRoot\Include\$SdkVersion\ucrt",
        "/I$SdkRoot\Include\$SdkVersion\shared",
        "/I$SdkRoot\Include\$SdkVersion\um",
        "/I$SdkRoot\Include\$SdkVersion\km"
    )
    $archFlags = if ($arch -eq 'x86') { @('/Gr', '/arch:SSE', '/DWIN32') } else { @('/DWIN64', '/D_WIN64') }
    $linkArgs = if ($arch -eq 'x86') {
        @('/link', '/opt:ref', '/safeseh:no', '/alternatename:__imp__CreateProcessInternalW@48=__imp__CreateProcessInternalW')
    } else {
        @('/link', '/opt:ref')
    }
    & $compiler /nologo /utf-8 /O2 /Gy /MT /EHs-c- /GR- /GS- /Zc:wchar_t $archFlags $includes `
        "$PSScriptRoot\elevation-locator-probe.cpp" `
        "$root\LocaleEmulatorPlus\Hooks\ElevationLocator.cpp" `
        "$root\out\libs\$arch\LepSupport.lib" `
        "$root\out\libs\$arch\lep_k32_$arch.lib" `
        "$root\out\libs\$arch\lep_ntdll_$arch.lib" `
        ntdll.lib kernel32.lib user32.lib `
        "/Fo$out/" "/Fe$out\elevation-locator-probe.exe" @linkArgs
    if ($LASTEXITCODE) { throw "Locator probe compile failed ($arch)." }

    $argsList = @()
    if ($arch -eq 'x64') {
        $samples = @(
            @('win7-x64',  "$root\out\uac-entry\shell32-win7-x64.dll",          'CD590',  '0'),
            @('win10-x64', "$root\out\uac-entry\Windows.Storage-win10-x64.dll", '1B7164', '0'),
            @('win11-x64', "$root\out\uac-entry\Windows.Storage-host-x64.dll",  '32CEB4', '0')
        )
    } else {
        $samples = @(
            @('win7-x86',  "$root\out\uac-entry\shell32-win7-x86.dll",                          'DE54C',  '0'),
            @('win10-x86', "$root\out\uac-entry\samples\win10-x86\Windows.Storage.dll",         '1C5510', '1'),
            @('win11-x86', "$root\out\uac-entry\samples\win11-x86\Windows.Storage.dll",         '1F69DC', '1')
        )
    }
    foreach ($s in $samples) {
        if (Test-Path -LiteralPath $s[1]) {
            $argsList += $s
        }
    }
    Write-Host "=== Running elevation-locator-probe ($arch) ==="
    & "$out\elevation-locator-probe.exe" @argsList
    if ($LASTEXITCODE) { throw "Locator probe failed ($arch)." }
}
