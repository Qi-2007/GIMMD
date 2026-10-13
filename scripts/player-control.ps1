param(
    [Parameter(Mandatory=$true)][string]$Directory,
    [Parameter(Mandatory=$true)][ValidateSet('play','pause','seek','loop-on','loop-off','stop','status')][string]$Command,
    [double]$Seconds = 0
)
$ErrorActionPreference = 'Stop'
$taskDirectory = (Resolve-Path -LiteralPath $Directory).Path
if ($Command -eq 'status') {
    Get-Content -LiteralPath (Join-Path $taskDirectory 'player.status.json') -Raw
    return
}
if ($Seconds -lt 0 -or $Seconds -gt 86400 -or [double]::IsNaN($Seconds)) { throw 'Invalid seek seconds.' }
$taskFile = Join-Path $taskDirectory 'player.command'
$taskSequence = 1
if (Test-Path -LiteralPath $taskFile) {
    $taskPrior = [IO.File]::ReadAllText($taskFile).Split(' ')[0]
    $taskSequence = [uint64]::Parse($taskPrior) + 1
}
$taskText = "$taskSequence $Command"
if ($Command -eq 'seek') { $taskText += ' ' + $Seconds.ToString([Globalization.CultureInfo]::InvariantCulture) }
$taskTemp = Join-Path $taskDirectory ('player-' + [guid]::NewGuid().ToString('N') + '.tmp')
[IO.File]::WriteAllText($taskTemp, $taskText, [Text.Encoding]::ASCII)
[IO.File]::Move($taskTemp, $taskFile, $true)
Write-Output $taskText
