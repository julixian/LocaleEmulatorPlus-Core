param(
    [string]$VcTools = 'D:\VisualStudio2026\VC\Tools\MSVC\14.44.35207',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $repoRoot 'out'
. "$PSScriptRoot\standard-modules.ps1"
$runtimeRoot = Join-Path $outputRoot 'timezone-test-runtime'
New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
foreach ($arch in @('x86','x64')) {
    $compiler = "$VcTools\bin\Hostx64\$arch\cl.exe"
    $env:LIB = "$VcTools\lib\$arch;$SdkRoot\Lib\$SdkVersion\ucrt\$arch;$SdkRoot\Lib\$SdkVersion\um\$arch"
    $includes = @("/I$VcTools\include", "/I$SdkRoot\Include\$SdkVersion\ucrt", "/I$SdkRoot\Include\$SdkVersion\shared", "/I$SdkRoot\Include\$SdkVersion\um")
    $standard = Build-StandardModules -Compiler $compiler -VcTools $VcTools -Includes $includes -OutputDirectory "$outputRoot\timezone-std-$arch"
    $references = $standard.References
    $language = $standard.Language
    $ifc = "$outputRoot\timezone-$arch.ifc"
    $moduleObject = "$outputRoot\timezone-module-$arch.obj"
    & $compiler /nologo /utf-8 /c $language /EHsc /MT $includes $references /interface "$repoRoot\include\LocaleEmulatorPlus.ixx" "/ifcOutput$ifc" "/Fo$moduleObject"
    if ($LASTEXITCODE -ne 0) { throw "Module compile failed: $arch" }
    $moduleObjects = @($moduleObject) + $standard.Objects
    & $compiler /nologo /utf-8 $language /EHsc /MT $includes $references /reference "LocaleEmulatorPlus=$ifc" "$PSScriptRoot\timezone-probe.cpp" $moduleObjects advapi32.lib "/Fo$outputRoot\timezone-probe-$arch.obj" "/Fe$outputRoot\timezone-probe-$arch.exe"
    if ($LASTEXITCODE -ne 0) { throw "Time-zone probe compile failed: $arch" }
    foreach ($name in @("LoaderDll_$arch.dll", "LocaleEmulatorPlus_$arch.dll")) {
        Copy-Item -LiteralPath "$outputRoot\$arch\$name" -Destination (Join-Path $runtimeRoot $name) -Force
    }
}
$cases = @('tokyo','korea','utc','pacific','southern','israel','uppercase',
    'standard-only','daylight-only','overrides','non-tokyo-overrides','max-names',
    'bad-size','oversize','short-header','bad-version','legacy-size','empty-id','unknown-id',
    'bad-path','slash','id-unterminated','standard-unterminated','daylight-unterminated',
    'bad-registry-mode','bad-ui-mode','null','nested','nested-pacific','nested-southern','nested-israel')
foreach ($arch in @('x86','x64')) {
    $other = if ($arch -eq 'x86') { 'x64' } else { 'x86' }
    foreach ($export in @('LepCreateProcess2','LepCreateProcess')) {
        foreach ($case in $cases) {
            & "$outputRoot\timezone-probe-$arch.exe" "$runtimeRoot\LoaderDll_$arch.dll" $case $export
            if ($LASTEXITCODE -ne 0) { throw "Time-zone probe failed: $arch/$case/$export exit=$LASTEXITCODE" }
        }
        foreach ($case in @('nested','nested-pacific','nested-southern','nested-israel')) {
            & "$outputRoot\timezone-probe-$arch.exe" "$runtimeRoot\LoaderDll_$arch.dll" $case $export "$outputRoot\timezone-probe-$other.exe"
            if ($LASTEXITCODE -ne 0) { throw "Time-zone cross-architecture probe failed: $arch->$other/$case/$export exit=$LASTEXITCODE" }
        }
    }
}
Write-Output 'Time-zone probes passed (140 cases, both architectures and both exports).'
