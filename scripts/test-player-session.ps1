param([Parameter(Mandatory=$true)][string]$Directory, [Parameter(Mandatory=$true)][int]$GameProcessId)
$ErrorActionPreference = 'Stop'
$taskDirectory = (Resolve-Path -LiteralPath $Directory).Path
$taskController = Join-Path $PSScriptRoot 'player-control.ps1'
$taskLoader = Start-Process -FilePath (Join-Path $taskDirectory 'gimmd-inject-probe.exe') -ArgumentList @('--pid', "$GameProcessId", ('"' + (Join-Path $taskDirectory 'gimmd-probe.dll') + '"')) -Verb RunAs -WindowStyle Hidden -PassThru
$taskDeadline = [DateTime]::UtcNow.AddSeconds(45)
function Read-PlayerStatus {
    $taskStatusPath = Join-Path $taskDirectory 'player.status.json'
    if (Test-Path -LiteralPath $taskStatusPath) {
        try { return (Get-Content -LiteralPath $taskStatusPath -Raw | ConvertFrom-Json) } catch { return $null }
    }
    return $null
}
function Wait-PlayerStatus([scriptblock]$Predicate) {
    while ([DateTime]::UtcNow -lt $taskDeadline) {
        $taskCurrent = Read-PlayerStatus
        if ($null -ne $taskCurrent -and (& $Predicate $taskCurrent)) { return $taskCurrent }
        if ($taskLoader.HasExited) { throw 'Loader exited before requested player state.' }
        Start-Sleep -Milliseconds 50
    }
    throw 'Player state timeout; the session has its own 90-second stop limit.'
}
try {
$taskEvents = @()
$taskEvents += Wait-PlayerStatus { param($s) $s.state -eq 1 -and $s.seconds -ge 1 }
& $taskController -Directory $taskDirectory -Command pause
$taskPaused = Wait-PlayerStatus { param($s) $s.state -eq 2 -and $s.acknowledgedSequence -eq 1 }
Start-Sleep -Milliseconds 350
$taskStillPaused = Read-PlayerStatus
if ($taskStillPaused.seconds -ne $taskPaused.seconds) { throw 'Timeline moved while paused.' }
$taskEvents += $taskPaused
& $taskController -Directory $taskDirectory -Command seek -Seconds 5
$taskEvents += Wait-PlayerStatus { param($s) $s.seconds -eq 5 -and $s.epoch -eq 1 -and $s.acknowledgedSequence -eq 2 }
& $taskController -Directory $taskDirectory -Command play
$taskEvents += Wait-PlayerStatus { param($s) $s.state -eq 1 -and $s.acknowledgedSequence -eq 3 }
& $taskController -Directory $taskDirectory -Command loop-on
$taskEvents += Wait-PlayerStatus { param($s) $s.acknowledgedSequence -eq 4 }
$taskEvents += Wait-PlayerStatus { param($s) $s.epoch -ge 2 -and $s.seconds -ge 0.5 -and $s.seconds -lt 3 }
& $taskController -Directory $taskDirectory -Command stop
if (-not $taskLoader.WaitForExit(5000)) { throw 'Loader did not finish after stop; do not retry concurrently.' }
if ($taskLoader.ExitCode -ne 0) { throw "Loader exit code: $($taskLoader.ExitCode)" }
$taskEvents | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskDirectory 'player-control-events.json') -Encoding UTF8
$taskReport = Get-Content -LiteralPath (Join-Path $taskDirectory "gimmd-$GameProcessId.probe.json") -Raw | ConvertFrom-Json
$taskReport.playerSession | ConvertTo-Json
$taskReport.singleTrackPlayback | Select-Object sampleCount,animationOverwriteCount,localRestored,cleanupPending | ConvertTo-Json
} catch {
    & $taskController -Directory $taskDirectory -Command stop | Out-Null
    throw
}
