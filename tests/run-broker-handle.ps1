param(
    [string]$VcTools = 'D:\VisualStudio2026\VC\Tools\MSVC\14.44.35207',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $repoRoot 'out'
$runtimeRoot = Join-Path $outputRoot 'broker-handle-runtime'
New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
foreach ($arch in @('x86','x64')) {
    $env:LIB = "$VcTools\lib\$arch;$SdkRoot\Lib\$SdkVersion\ucrt\$arch;$SdkRoot\Lib\$SdkVersion\um\$arch"
    & "$VcTools\bin\Hostx64\$arch\cl.exe" /nologo /utf-8 /MT /EHsc /O2 "/I$VcTools\include" "/I$SdkRoot\Include\$SdkVersion\ucrt" "/I$SdkRoot\Include\$SdkVersion\shared" "/I$SdkRoot\Include\$SdkVersion\um" "$PSScriptRoot\broker-handle-probe.cpp" "/Fo$outputRoot\broker-handle-probe-$arch.obj" "/Fe$outputRoot\broker-handle-probe-$arch.exe"
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $arch" }
    foreach ($name in @("LoaderDll_$arch.dll", "LocaleEmulatorPlus_$arch.dll")) {
        Copy-Item -LiteralPath "$outputRoot\$arch\$name" -Destination (Join-Path $runtimeRoot $name) -Force
    }
    foreach ($case in @('created-handle','fallback','restricted','wrong-pid','bad-pid')) {
        Write-Output "$arch/$case"
        & "$outputRoot\broker-handle-probe-$arch.exe" "$runtimeRoot\LoaderDll_$arch.dll" $case
        if ($LASTEXITCODE -ne 0) { throw "Broker probe failed: $arch/$case exit=$LASTEXITCODE" }
    }
}
