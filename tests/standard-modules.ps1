function Build-StandardModules {
    param(
        [string]$Compiler,
        [string]$VcTools,
        [string[]]$Includes,
        [string]$OutputDirectory
    )
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $references = @()
    $objects = @()
    foreach ($name in @('std', 'std.compat')) {
        $ifc = Join-Path $OutputDirectory "$name.ifc"
        $object = Join-Path $OutputDirectory "$name.obj"
        & $Compiler /nologo /utf-8 /c /std:c++latest /EHsc /MT $Includes $references /interface "$VcTools\modules\$name.ixx" "/ifcOutput$ifc" "/Fo$object" | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Standard module compile failed: $name" }
        $references += @('/reference', "$name=$ifc")
        $objects += $object
    }
    [PSCustomObject]@{ Language = '/std:c++latest'; References = $references; Objects = $objects }
}
