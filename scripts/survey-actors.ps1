param([string]$BuildDirectory = 'build-actor-survey')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskBuild = [IO.Path]::GetFullPath((Join-Path $taskRoot $BuildDirectory))
$taskManifest = Get-Content -LiteralPath (Join-Path $taskBuild 'build-manifest.json') -Raw | ConvertFrom-Json
if (-not $taskManifest.actorSurvey -or $taskManifest.playerSession) { throw 'A read-only ActorSurvey build is required.' }
$taskGames = @(Get-Process -Name YuanShen -ErrorAction SilentlyContinue)
if ($taskGames.Count -ne 1) { throw 'Open exactly one YuanShen process and enter a scene first.' }
$taskGame = $taskGames[0]
$taskSession = Join-Path $taskRoot ('sessions/actor-survey-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $taskSession | Out-Null
$taskDll = Join-Path $taskSession 'gimmd-probe.dll'
Copy-Item -LiteralPath (Join-Path $taskBuild 'gimmd-probe.dll') -Destination $taskDll
if ($taskManifest.resourceTrace -and (Test-Path -LiteralPath (Join-Path $taskBuild 'resource-trace-seconds.txt'))) {
    Copy-Item -LiteralPath (Join-Path $taskBuild 'resource-trace-seconds.txt') -Destination (Join-Path $taskSession 'resource-trace-seconds.txt')
}
Write-Output "Survey running: $taskSession"
$taskLoader = Start-Process -FilePath (Join-Path $taskBuild 'gimmd-inject-probe.exe') -ArgumentList @('--pid', "$($taskGame.Id)", ('"' + $taskDll + '"')) -Verb RunAs -WindowStyle Hidden -PassThru
$taskTimeout = if ($taskManifest.resourceTrace) { 630000 } else { 45000 }
if (-not $taskLoader.WaitForExit($taskTimeout)) { throw "Survey still running; report directory: $taskSession" }
if ($taskLoader.ExitCode -ne 0) { throw "Survey loader failed ($($taskLoader.ExitCode)); report directory: $taskSession" }
$taskReport = Join-Path $taskSession "gimmd-$($taskGame.Id).probe.json"
$taskSnapshot = Get-Content -LiteralPath $taskReport -Raw | ConvertFrom-Json
if ($taskSnapshot.settersCalled) { throw 'Unexpected setter call in read-only survey.' }
$taskPython = 'C:\Users\L\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
& $taskPython (Join-Path $PSScriptRoot 'analyze-actors.py') $taskReport --output (Join-Path $taskSession 'actors.json')
if ($LASTEXITCODE -ne 0) { throw 'Actor report analysis failed.' }
if (Select-String -LiteralPath $taskReport -Pattern '"resourceLookupStatus"' -Quiet) {
    & $taskPython (Join-Path $PSScriptRoot 'analyze-resources.py') $taskReport --output (Join-Path $taskSession 'resources.json')
    if ($LASTEXITCODE -ne 0) { throw 'Resource report analysis failed.' }
}
Write-Output "Survey saved: $taskSession"
