[CmdletBinding()]
param(
    [ValidateRange(1, 65535)]
    [int]$Port = 39000,

    [ValidateRange(1, 1000)]
    [int]$TickRate = 30,

    [ValidateRange(1, 1000)]
    [int]$SnapshotRate = 15,

    [ValidateRange(0, [int]::MaxValue)]
    [int]$Ticks = 0,

    [switch]$Help
)

$serverPath = Join-Path $PSScriptRoot 'server\build\msvc-debug\Debug\bored_server.exe'

if ($Help) {
    Get-Help $PSCommandPath -Detailed
    exit 0
}

if (-not (Test-Path -LiteralPath $serverPath -PathType Leaf)) {
    Write-Error "Server executable not found: $serverPath`nBuild it first:`n  cmake --preset windows-msvc-debug`n  cmake --build --preset windows-msvc-debug"
    exit 1
}

$serverArguments = @(
    '--port', $Port,
    '--tick-rate', $TickRate,
    '--snapshot-rate', $SnapshotRate
)

if ($Ticks -gt 0) {
    $serverArguments += @('--ticks', $Ticks)
}

Write-Host "Starting bored_server: port=$Port, tick-rate=$TickRate, snapshot-rate=$SnapshotRate"
if ($Ticks -gt 0) {
    Write-Host "This run will exit after $Ticks ticks."
}

# Run directly so Ctrl+C is passed through to the C++ server.
& $serverPath @serverArguments
exit $LASTEXITCODE
