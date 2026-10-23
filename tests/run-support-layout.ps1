param(
    [string]$VcTools = 'D:\VisualStudio2026\VC\Tools\MSVC\14.44.35207',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.26100.0',
    [string]$BaselineHeader,
    [switch]$UpdateBaseline
)
$ErrorActionPreference = 'Stop'
if ($UpdateBaseline -and !$BaselineHeader) { throw 'Updating the fixture requires an original baseline header.' }
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $repoRoot 'out'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
foreach ($arch in @('x86','x64')) {
    $compiler = "$VcTools\bin\Hostx64\$arch\cl.exe"
    $env:LIB = "$VcTools\lib\$arch;$SdkRoot\Lib\$SdkVersion\ucrt\$arch;$SdkRoot\Lib\$SdkVersion\um\$arch"
    $includes = @("/I$VcTools\include", "/I$SdkRoot\Include\$SdkVersion\ucrt", "/I$SdkRoot\Include\$SdkVersion\shared", "/I$SdkRoot\Include\$SdkVersion\um", "/I$SdkRoot\Include\$SdkVersion\km")
    $fixture = Join-Path $PSScriptRoot "support-layout-$arch.txt"
    $modes = if ($BaselineHeader) { @('baseline','current') } else { @('current') }
    foreach ($mode in $modes) {
        $header = if ($mode -eq 'baseline') { (Resolve-Path -LiteralPath $BaselineHeader).Path } else { "$repoRoot\LocaleEmulatorPlus\support\Support.h" }
        $exe = "$outputRoot\support-layout-$arch-$mode.exe"
        & $compiler /nologo /MT /O2 /DML_DISABLE_THIRD_LIB=1 /D_NO_CRT_STDIO_INLINE $includes "/FI$header" "$PSScriptRoot\support-layout.cpp" "/Fo$outputRoot\support-layout-$arch-$mode.obj" "/Fe$exe" /link legacy_stdio_definitions.lib
        if ($LASTEXITCODE -ne 0) { throw "Layout probe compile failed: $arch/$mode" }
        $result = (& $exe) -join "`n"
        if ($LASTEXITCODE -ne 0) { throw "Layout probe failed: $arch/$mode" }
        if ($mode -eq 'baseline') {
            $expected = $result
            if ($UpdateBaseline) { [IO.File]::WriteAllText($fixture, $expected + "`n") }
        } else {
            if (!$BaselineHeader) { $expected = [IO.File]::ReadAllText($fixture).TrimEnd("`r", "`n") }
            if ($result -cne $expected) { throw "Native layout changed: $arch`nExpected:`n$expected`nActual:`n$result" }
        }
    }
    Write-Output "${arch}: 26 native sizes/offsets match the original baseline"
}
