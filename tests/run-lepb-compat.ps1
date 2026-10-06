param(
    [string]$VcTools = 'D:\VisualStudio2026\VC\Tools\MSVC\14.44.35207',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $repoRoot 'out'
$runtimeRoot = Join-Path $outputRoot 'lepb-test-runtime'
New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
foreach ($arch in @('x86','x64')) {
    $env:LIB = "$VcTools\lib\$arch;$SdkRoot\Lib\$SdkVersion\ucrt\$arch;$SdkRoot\Lib\$SdkVersion\um\$arch"
    & "$VcTools\bin\Hostx64\$arch\cl.exe" /nologo /MT /EHsc /O2 "/I$VcTools\include" "/I$SdkRoot\Include\$SdkVersion\ucrt" "/I$SdkRoot\Include\$SdkVersion\shared" "/I$SdkRoot\Include\$SdkVersion\um" "$PSScriptRoot\lepb-compat-probe.cpp" "/Fo$outputRoot\lepb-probe-$arch.obj" "/Fe$outputRoot\lepb-probe-$arch.exe"
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $arch" }
    foreach ($name in @("LoaderDll_$arch.dll", "LocaleEmulatorPlus_$arch.dll")) {
        Copy-Item -LiteralPath "$outputRoot\$arch\$name" -Destination (Join-Path $runtimeRoot $name) -Force
    }
}
foreach ($arch in @('x86','x64')) {
    foreach ($case in @('v2','v2-nested','v3','empty','korea','uppercase','nested','bad-size','bad-version','v3-short','v2-long','unterminated','unknown-id','bad-bias','bad-standard-bias','bad-path')) {
        Write-Output "$arch/$case"
        & "$outputRoot\lepb-probe-$arch.exe" "$runtimeRoot\LoaderDll_$arch.dll" $case
        if ($LASTEXITCODE -ne 0) { throw "Probe failed: $arch/$case exit=$LASTEXITCODE" }
    }
    $other = if ($arch -eq 'x86') { 'x64' } else { 'x86' }
    Write-Output "$arch -> $other child propagation"
    & "$outputRoot\lepb-probe-$arch.exe" "$runtimeRoot\LoaderDll_$arch.dll" nested "$outputRoot\lepb-probe-$other.exe"
    if ($LASTEXITCODE -ne 0) { throw "Cross-architecture probe failed: $arch -> $other exit=$LASTEXITCODE" }
}
