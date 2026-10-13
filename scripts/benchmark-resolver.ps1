param([int]$GameProcessId = 6020, [string]$Baseline, [string]$Optimized)
$ErrorActionPreference='Stop'
$taskController=Join-Path $PSScriptRoot 'player-control.ps1'
function Start-Probe([string]$Directory) {
    $taskDll=Join-Path $Directory 'gimmd-probe.dll'
    return Start-Process -FilePath (Join-Path $Directory 'gimmd-inject-probe.exe') -ArgumentList @('--pid',"$GameProcessId",('"'+$taskDll+'"')) -WindowStyle Hidden -PassThru
}
$taskLoader=Start-Probe $Baseline
$taskDeadline=[DateTime]::UtcNow.AddSeconds(15)
while ([DateTime]::UtcNow -lt $taskDeadline) {
    $taskStatus=$null
    try { $taskStatus=Get-Content -LiteralPath (Join-Path $Baseline 'player.status.json') -Raw | ConvertFrom-Json } catch {}
    if ($taskStatus -and $taskStatus.state -eq 1 -and $taskStatus.seconds -ge 3) { break }
    if ($taskLoader.HasExited) { throw 'Baseline probe ended before playback; inspect its report before retrying.' }
    Start-Sleep -Milliseconds 50
}
& $taskController -Directory $Baseline -Command stop | Out-Null
if (-not $taskLoader.WaitForExit(5000)) { throw 'Baseline stop pending; optimized probe was not started.' }
$taskBaselineReport=Get-Content -LiteralPath (Join-Path $Baseline "gimmd-$GameProcessId.probe.json") -Raw | ConvertFrom-Json
if ($taskLoader.ExitCode -ne 0 -or -not $taskBaselineReport.singleTrackPlayback.localRestored -or
    $taskBaselineReport.singleTrackPlayback.cleanupPending -or -not $taskBaselineReport.singleTrackPlayback.animatorLease.restored) { throw 'Baseline restoration failed.' }
$taskLoader=Start-Probe $Optimized
if (-not $taskLoader.WaitForExit(30000)) { throw 'Optimized probe still running; do not launch concurrently.' }
$taskFastReport=Get-Content -LiteralPath (Join-Path $Optimized "gimmd-$GameProcessId.probe.json") -Raw | ConvertFrom-Json
$taskResult=[ordered]@{baseline=$taskBaselineReport.singleTrackPlayback.multiTrackPreview.performance;
    optimized=$taskFastReport.singleTrackPlayback.multiTrackPreview.performance;
    optimizedSamples=$taskFastReport.singleTrackPlayback.sampleCount;
    optimizedOverwrites=$taskFastReport.singleTrackPlayback.animationOverwriteCount;
    optimizedRestored=$taskFastReport.singleTrackPlayback.localRestored;
    optimizedAnimatorRestored=$taskFastReport.singleTrackPlayback.animatorLease.restored;
    optimizedExitCode=$taskLoader.ExitCode}
$taskResult | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Optimized 'resolver-benchmark.json') -Encoding UTF8
if ($taskLoader.ExitCode -ne 0 -or -not $taskResult.optimizedRestored -or -not $taskResult.optimizedAnimatorRestored) { throw 'Optimized restoration failed.' }
