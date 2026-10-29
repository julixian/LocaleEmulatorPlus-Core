param(
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$ProcessId,
    [Parameter(Mandatory)][string]$DllDirectory,
    [switch]$Disable
)
$ErrorActionPreference = 'Stop'
$directory = (Resolve-Path -LiteralPath $DllDirectory).Path
$logDirectory = Join-Path $directory 'Log'
$marker = Join-Path $logDirectory "input-trace.$ProcessId.enable"
if ($Disable) {
    if (Test-Path -LiteralPath $marker) { Remove-Item -LiteralPath $marker }
    Write-Output "Disabled: $marker"
} else {
    $target = Get-Process -Id $ProcessId -ErrorAction Stop
    New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
    [IO.File]::WriteAllText($marker, '')
    Write-Output "Enabled for $($target.ProcessName), PID $ProcessId : $marker"
}
Write-Output "UTC: $([DateTime]::UtcNow.ToString('o'))"
