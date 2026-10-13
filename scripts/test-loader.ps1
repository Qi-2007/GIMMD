$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskBuild = Join-Path $taskRoot 'build'
$probeDll = Join-Path $taskBuild 'gimmd-probe.dll'
$hostProcess = Start-Process -FilePath (Join-Path $taskBuild 'gimmd-probe-host.exe') -ArgumentList '--wait' -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskBuild 'host.stdout.log') -RedirectStandardError (Join-Path $taskBuild 'host.stderr.log')
try {
    Start-Sleep -Milliseconds 250
    & (Join-Path $taskBuild 'gimmd-inject-probe.exe') --pid $hostProcess.Id $probeDll
    if ($LASTEXITCODE -ne 0) { throw 'Remote probe load failed in own host.' }
    $reportPath = Join-Path $taskBuild "gimmd-$($hostProcess.Id).probe.json"
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if ($report.pid -ne $hostProcess.Id -or -not $report.probeOnly -or $report.engineCallsMade) {
        throw 'Probe report contract failed.'
    }
    if (-not $report.modules[0].loaded -or $report.modules[1].loaded) {
        throw 'Host module report is unexpected.'
    }
    # A second call exercises already-loaded module handling and explicit probe.
    & (Join-Path $taskBuild 'gimmd-inject-probe.exe') --pid $hostProcess.Id $probeDll
    if ($LASTEXITCODE -ne 0) { throw 'Repeated probe failed.' }
    Write-Output 'Remote LoadLibrary, report and repeated-probe checks passed in own host.'
} finally {
    try {
        $stopEvent = [System.Threading.EventWaitHandle]::OpenExisting("Local\GimmdProbeHostStop-$($hostProcess.Id)")
        try { $stopEvent.Set() | Out-Null } finally { $stopEvent.Dispose() }
        if (-not $hostProcess.WaitForExit(5000)) { Write-Warning 'Host will exit at its own 60-second timeout.' }
    } catch { Write-Warning "Host stop signal unavailable: $_" }
    $hostProcess.Dispose()
}
