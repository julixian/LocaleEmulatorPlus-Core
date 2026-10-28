param(
    [string]$VcTools = 'D:\VisualStudio2026\VC\Tools\MSVC\14.44.35207',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $repoRoot 'out\input-path'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
. "$PSScriptRoot\standard-modules.ps1"
foreach ($arch in @('x86', 'x64')) {
    $compiler = "$VcTools\bin\Hostx64\$arch\cl.exe"
    $env:LIB = "$VcTools\lib\$arch;$SdkRoot\Lib\$SdkVersion\ucrt\$arch;$SdkRoot\Lib\$SdkVersion\um\$arch"
    $includes = @("/I$VcTools\include", "/I$SdkRoot\Include\$SdkVersion\ucrt", "/I$SdkRoot\Include\$SdkVersion\shared", "/I$SdkRoot\Include\$SdkVersion\um")
    $standard = Build-StandardModules -Compiler $compiler -VcTools $VcTools -Includes $includes -OutputDirectory "$outputRoot\std-$arch"
    $references = $standard.References
    $language = $standard.Language
    $ifc = "$outputRoot\lep-$arch.ifc"
    $moduleObject = "$outputRoot\lep-$arch.obj"
    & $compiler /nologo /utf-8 /c $language /EHsc /MT $includes $references /interface "$repoRoot\include\LocaleEmulatorPlus.ixx" "/ifcOutput$ifc" "/Fo$moduleObject"
    if ($LASTEXITCODE -ne 0) { throw "Module compile failed: $arch" }
    $moduleObjects = @($moduleObject) + $standard.Objects
    & $compiler /nologo /utf-8 $language /EHsc /MT $includes $references /reference "LocaleEmulatorPlus=$ifc" "$PSScriptRoot\input-path-probe.cpp" $moduleObjects user32.lib "/Fo$outputRoot\probe-$arch.obj" "/Fe$outputRoot\probe-$arch.exe"
    if ($LASTEXITCODE -ne 0) { throw "Probe compile failed: $arch" }
    foreach ($name in @("LoaderDll_$arch.dll", "LocaleEmulatorPlus_$arch.dll")) {
        Copy-Item -LiteralPath "$repoRoot\out\$arch\$name" -Destination $outputRoot -Force
    }
    & "$outputRoot\probe-$arch.exe" "$outputRoot\LoaderDll_$arch.dll" 2>&1 | Tee-Object -FilePath "$outputRoot\results-$arch.log"
    if ($LASTEXITCODE -ne 0) { throw "Probe execution failed: $arch exit=$LASTEXITCODE" }
}
Write-Output 'Input-path regression probes passed (native and LEP CP936/932/1252, x86/x64).'
