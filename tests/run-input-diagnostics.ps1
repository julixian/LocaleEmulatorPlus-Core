param(
    [string]$VcTools = 'D:\VisualStudio2026\VC\Tools\MSVC\14.44.35207',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.26100.0',
    [switch]$ReuseDlls
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot\standard-modules.ps1"
foreach ($arch in @('x86','x64')) {
    $output = "$root\out\input-diagnostics\$arch"
    New-Item -ItemType Directory -Path "$output\Log" -Force | Out-Null
    $compiler = "$VcTools\bin\Hostx64\$arch\cl.exe"
    $env:LIB = "$VcTools\lib\$arch;$SdkRoot\Lib\$SdkVersion\ucrt\$arch;$SdkRoot\Lib\$SdkVersion\um\$arch"
    $includes = @("/I$VcTools\include","/I$SdkRoot\Include\$SdkVersion\ucrt","/I$SdkRoot\Include\$SdkVersion\shared","/I$SdkRoot\Include\$SdkVersion\um")
    $standard = Build-StandardModules -Compiler $compiler -VcTools $VcTools -Includes $includes -OutputDirectory "$output\std"
    $references = $standard.References
    & $compiler /nologo /utf-8 /c /std:c++latest /EHsc /MT $includes $references /interface "$root\include\LocaleEmulatorPlus.ixx" "/ifcOutput$output\lep.ifc" "/Fo$output\lep.obj"
    if ($LASTEXITCODE) { throw "Public module compile failed: $arch" }
    & $compiler /nologo /utf-8 /std:c++latest /EHsc /MT $includes $references /reference "LocaleEmulatorPlus=$output\lep.ifc" "$PSScriptRoot\input-diagnostics-probe.cpp" "$output\lep.obj" $standard.Objects user32.lib "/Fo$output\probe.obj" "/Fe$output\input-diagnostics-probe.exe"
    if ($LASTEXITCODE) { throw "Diagnostic probe compile failed: $arch" }
    if (!$ReuseDlls) {
        foreach ($name in @("LoaderDll_$arch.dll","LocaleEmulatorPlus_$arch.dll")) {
            Copy-Item -LiteralPath "$root\out\$arch\$name" -Destination $output -Force
        }
    }
    $started = [DateTime]::UtcNow
    & "$output\input-diagnostics-probe.exe" "$output\LoaderDll_$arch.dll" 2>&1 | Tee-Object "$output\selftest.log"
    if ($LASTEXITCODE) { throw "Diagnostic probe failed: $arch exit=$LASTEXITCODE" }
    $logs = @(Get-ChildItem -LiteralPath "$output\Log" -File | Where-Object { $_.LastWriteTimeUtc -ge $started -and $_.Name -like '*.log.txt' })
    $text = ($logs | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
    foreach ($pattern in @('inputdiag-ready version=1','event=message','event=convert','event=peek-native','event=edit-trail','event=clipboard','lastError=1418')) {
        if ($text -notmatch [regex]::Escape($pattern)) { throw "Missing diagnostic output: $arch $pattern (build with --diag-input)" }
    }
    if ($text -match 'unicode=0042|unicode=0043') { throw "Unarmed character was logged: $arch" }
    if (Get-ChildItem -LiteralPath "$output\Log" -Filter '*.enable' -File) { throw "Arm marker leaked: $arch" }
    Write-Output "Input diagnostics self-test passed: $arch"
}
