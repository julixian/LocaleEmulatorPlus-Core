param(
    [string]$VcTools = 'D:\VisualStudio2026\VC\Tools\MSVC\14.44.35207',
    [string]$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10',
    [string]$SdkVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $repoRoot 'out\support-header-tests'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

# These are supported include entry points. Low-level NT API declaration
# fragments are assembled by native/Declarations.h.
$headers = @(
    'base/Platform.h', 'base/Pointer.h',
    'native/Declarations.h', 'native/Helpers.h', 'native/Path.h', 'native/RegistrySupport.h', 'native/Operations.h',
    'runtime/MemoryPrimitives.h', 'runtime/ActiveFrame.h', 'runtime/Heap.h', 'runtime/File.h',
    'runtime/TypeTraits.h', 'runtime/Array.h', 'runtime/String.h',
    'runtime/HashTable.h', 'runtime/Diagnostics.h',
    'pe/ExportHash.h', 'pe/Image.h', 'patch/Decoder.h', 'patch/MemoryPatch.h',
    'search/Sunday.h', 'search/Pattern.h', 'Support.h'
)
foreach ($arch in @('x86', 'x64')) {
    $compiler = "$VcTools\bin\Hostx64\$arch\cl.exe"
    $includes = @("/I$repoRoot\LocaleEmulatorPlus\support", "/I$VcTools\include", "/I$SdkRoot\Include\$SdkVersion\ucrt", "/I$SdkRoot\Include\$SdkVersion\shared", "/I$SdkRoot\Include\$SdkVersion\um", "/I$SdkRoot\Include\$SdkVersion\km")
    foreach ($header in $headers) {
        $source = Join-Path $outputRoot "$arch.cpp"
        [IO.File]::WriteAllText($source, "#include `"$header`"`n#include `"$header`"`n")
        & $compiler /nologo /utf-8 /Zs /MT /DENABLE_LOG=1 /D_NO_CRT_STDIO_INLINE $includes $source
        if ($LASTEXITCODE -ne 0) { throw "Header is not self-contained: $arch/$header" }
    }
    Write-Output "${arch}: $($headers.Count) support entry headers compile independently and tolerate repeated inclusion"
}
