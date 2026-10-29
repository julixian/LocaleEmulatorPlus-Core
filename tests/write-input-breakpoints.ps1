param(
    [Parameter(Mandatory)][ValidateSet('x86','x64')][string]$Architecture,
    [Parameter(Mandatory)][string]$OutputFile
)
$ErrorActionPreference = 'Stop'
if ($Architecture -eq 'x86') {
    $himc='poi(@esp+4)'; $index='poi(@esp+8)'; $buffer='poi(@esp+0xc)'; $capacity='poi(@esp+0x10)'
    $first='poi(@esp+4)'
} else {
    $himc='@rcx'; $index='@rdx'; $buffer='@r8'; $capacity='@r9'; $first='@rcx'
}
$lines = @('.echo LEP input observation - entry-only trace without automatic return capture.',
           '.echo Run after login and remove these breakpoints at the end of the test.')
foreach ($suffix in @('A','W')) {
    $lines += 'bu imm32!ImmGetCompositionString' + $suffix + ' ".printf \"[IME-' + $suffix + '] tid=%x himc=%p index=%x buf=%p cap=%x\", @$tid, ' + $himc + ', ' + $index + ', ' + $buffer + ', ' + $capacity + '; .echo; kn 4; gc"'
}
$lines += 'bu user32!GetClipboardData ".printf \"[CLIP] tid=%x format=%x\", @$tid, ' + $first + '; .echo; kn 4; gc"'
$lines += 'bu user32!GetSystemMetrics ".if (' + $first + ' == 0x2a) { .printf \"[DBCS] tid=%x SM_DBCSENABLED\", @$tid; .echo; kn 4; }; gc"'
$lines += 'bu user32!CheckDBCSEnabledExt ".printf \"[DBCS] tid=%x CheckDBCSEnabledExt\", @$tid; .echo; kn 4; gc"'
$lines += '.echo LEP input observation breakpoints installed.'
$parent = Split-Path -Parent ([IO.Path]::GetFullPath($OutputFile))
New-Item -ItemType Directory -Path $parent -Force | Out-Null
$lines | Set-Content -LiteralPath $OutputFile -Encoding ASCII
Write-Output ([IO.Path]::GetFullPath($OutputFile))
