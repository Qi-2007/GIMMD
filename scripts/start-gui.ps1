param([string]$PythonPath, [string]$Session, [string]$Project)
$ErrorActionPreference='Stop'
if (-not $PythonPath) {
    $PythonPath=Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/pythonw.exe'
}
if (-not (Test-Path -LiteralPath $PythonPath)) { throw 'Use -PythonPath to select Python 3.10+ with tkinter (pythonw.exe).' }
$taskScript=Join-Path (Split-Path -Parent $PSScriptRoot) 'src/gui/player_gui.py'
$taskArguments=@(('"'+$taskScript+'"'))
if ($Session) { $taskArguments+=@('--session',('"'+(Resolve-Path -LiteralPath $Session).Path+'"')) }
if ($Project) { $taskArguments+=@('--project',('"'+(Resolve-Path -LiteralPath $Project).Path+'"')) }
$taskLogDirectory=Join-Path (Split-Path -Parent $PSScriptRoot) 'logs'
New-Item -ItemType Directory -Path $taskLogDirectory -Force | Out-Null
$taskLogStem=Join-Path $taskLogDirectory ('gui-'+[guid]::NewGuid().ToString('N'))
$taskProcess=Start-Process -FilePath $PythonPath -ArgumentList $taskArguments -WindowStyle Hidden -PassThru -RedirectStandardError ($taskLogStem+'.stderr.log') -RedirectStandardOutput ($taskLogStem+'.stdout.log')
if ($taskProcess.WaitForExit(1500) -and $taskProcess.ExitCode -ne 0) {
    throw ('GUI startup failed. See '+$taskLogStem+'.stderr.log')
}
