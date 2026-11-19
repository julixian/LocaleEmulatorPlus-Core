param(
    [ValidateSet('x86','x64')][string]$Architecture = 'x64',
    [ValidateSet('runas','manifest')][string]$Mode = 'runas',
    [switch]$BuildOnly,
    [string]$VcTools = 'D:\VisualStudio2026\VC\Tools\MSVC\14.44.35207',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
if ([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this test from a non-elevated terminal.'
}
$root = Split-Path -Parent $PSScriptRoot
$output = "$root\out\elevation\$Architecture"
$result = "$output\$Mode-$(Get-Date -Format yyyyMMdd-HHmmss)"
New-Item -ItemType Directory -Path $result -Force | Out-Null
. "$PSScriptRoot\standard-modules.ps1"
$compiler = "$VcTools\bin\Hostx64\$Architecture\cl.exe"
$env:PATH = "$SdkRoot\bin\$SdkVersion\x64;$VcTools\bin\Hostx64\$Architecture;$env:PATH"
$env:LIB = "$VcTools\lib\$Architecture;$SdkRoot\Lib\$SdkVersion\ucrt\$Architecture;$SdkRoot\Lib\$SdkVersion\um\$Architecture"
$includes = @("/I$VcTools\include","/I$SdkRoot\Include\$SdkVersion\ucrt","/I$SdkRoot\Include\$SdkVersion\shared","/I$SdkRoot\Include\$SdkVersion\um")
$standard = Build-StandardModules -Compiler $compiler -VcTools $VcTools -Includes $includes -OutputDirectory "$output\std"
$references = $standard.References
& $compiler /nologo /utf-8 /c /std:c++latest /EHsc /MT $includes $references /interface "$root\include\LocaleEmulatorPlus.ixx" "/ifcOutput$output\lep.ifc" "/Fo$output\lep.obj"
if ($LASTEXITCODE) { throw 'Public module compile failed.' }
& $compiler /nologo /utf-8 /std:c++latest /EHsc /MT $includes $references /reference "LocaleEmulatorPlus=$output\lep.ifc" "$PSScriptRoot\elevation-probe.cpp" "$output\lep.obj" $standard.Objects shell32.lib ole32.lib advapi32.lib user32.lib "/Fo$output\probe.obj" "/Fe$output\elevation-probe.exe" /link /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
if ($LASTEXITCODE) { throw 'Probe compile failed.' }
$target = "$output\elevation-probe.exe"
if ($Mode -eq 'manifest') {
    & $compiler /nologo /utf-8 /std:c++latest /EHsc /MT $includes $references /reference "LocaleEmulatorPlus=$output\lep.ifc" "$PSScriptRoot\elevation-probe.cpp" "$output\lep.obj" $standard.Objects shell32.lib ole32.lib advapi32.lib user32.lib "/Fo$output\admin.obj" "/Fe$output\elevation-probe-admin.exe" /link /MANIFEST:EMBED "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
    if ($LASTEXITCODE) { throw 'Admin probe compile failed.' }
    $target = "$output\elevation-probe-admin.exe"
}
foreach ($name in @("LoaderDll_$Architecture.dll","LocaleEmulatorPlus_$Architecture.dll")) {
    Copy-Item -LiteralPath "$root\out\$Architecture\$name" -Destination $output -Force
}
Get-FileHash -LiteralPath "$output\LoaderDll_$Architecture.dll","$output\LocaleEmulatorPlus_$Architecture.dll",$target | Select-Object Path,Hash | ConvertTo-Json | Set-Content "$result\binaries.json" -Encoding UTF8
git -C $root rev-parse HEAD | Set-Content "$result\source-commit.txt"
if ($BuildOnly) { Write-Host "Prepared $Architecture/$Mode probe: $target"; return }
Write-Host "UAC test $Architecture/$Mode. Approve the LEP elevation probe prompt. Results: $result"
& "$output\elevation-probe.exe" "$output\LoaderDll_$Architecture.dll" $result $target $Mode
if ($LASTEXITCODE) { throw "Probe launch failed/cancelled: exit=$LASTEXITCODE. See $result" }
$records = foreach ($stage in @('native','parent','ordinary-child','elevated','elevated-grandchild')) {
    Get-Content -LiteralPath "$result\$stage.json" -Raw | ConvertFrom-Json
}
$records | Format-Table stage,pid,arch,elevated,integrityRid,acp,oemcp,threadLcid,userLcid,systemLcid,uiLanguage | Out-Host
foreach ($record in $records) {
    $log = "$output\Log\elevation-probe.exe.$('{0:X}' -f [int]$record.pid).log.txt"
    if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination $result }
}
if ($records[0].elevated -or $records[1].elevated -or $records[1].acp -ne 932 -or $records[2].acp -ne 932) { throw 'Native/parent/ordinary-child control failed.' }
if (!$records[3].elevated -or !$records[4].elevated) { throw 'UAC elevation not established.' }
$summary = [PSCustomObject]@{
    Architecture=$Architecture; Mode=$Mode; NativeAcp=$records[0].acp
    ParentAcp=$records[1].acp; OrdinaryChildAcp=$records[2].acp
    ElevatedChildAcp=$records[3].acp; ElevatedGrandchildAcp=$records[4].acp
    Propagated=($records[3].acp -eq 932 -and $records[3].userLcid -eq 0x411)
}
$summary | ConvertTo-Json | Tee-Object "$result\summary.json"
