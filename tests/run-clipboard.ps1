param(
    [string]$VcTools = 'D:\VisualStudio2026\VC\Tools\MSVC\14.44.35207',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot\standard-modules.ps1"
foreach ($arch in @('x86','x64')) {
    $output = "$root\out\clipboard-tests\$arch"
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    $compiler = "$VcTools\bin\Hostx64\$arch\cl.exe"
    $env:LIB = "$VcTools\lib\$arch;$SdkRoot\Lib\$SdkVersion\ucrt\$arch;$SdkRoot\Lib\$SdkVersion\um\$arch"
    $includes = @("/I$VcTools\include", "/I$SdkRoot\Include\$SdkVersion\ucrt", "/I$SdkRoot\Include\$SdkVersion\shared", "/I$SdkRoot\Include\$SdkVersion\um")
    $standard = Build-StandardModules -Compiler $compiler -VcTools $VcTools -Includes $includes -OutputDirectory "$output\std"
    & $compiler /nologo /utf-8 /c /std:c++latest /EHsc /MT $includes $standard.References /interface "$root\include\LocaleEmulatorPlus.ixx" "/ifcOutput$output\lep.ifc" "/Fo$output\lep.obj"
    if ($LASTEXITCODE -ne 0) { throw "Public module compile failed: $arch" }
    & $compiler /nologo /utf-8 /std:c++latest /EHsc /MT $includes $standard.References /reference "LocaleEmulatorPlus=$output\lep.ifc" "$PSScriptRoot\clipboard-probe.cpp" "$output\lep.obj" $standard.Objects user32.lib "/Fo$output\probe.obj" "/Fe$output\clipboard-probe.exe"
    if ($LASTEXITCODE -ne 0) { throw "Clipboard probe compile failed: $arch" }
    foreach ($name in @("LoaderDll_$arch.dll", "LocaleEmulatorPlus_$arch.dll")) {
        Copy-Item -LiteralPath "$root\out\$arch\$name" -Destination $output -Force
    }
    & "$output\clipboard-probe.exe" "$output\LoaderDll_$arch.dll" 2>&1 | Tee-Object "$output\results.log"
    if ($LASTEXITCODE -ne 0) { throw "Clipboard probe failed: $arch exit=$LASTEXITCODE" }
    Write-Output "Clipboard regression passed: $arch"
}
