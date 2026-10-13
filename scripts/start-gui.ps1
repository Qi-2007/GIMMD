param([string]$PythonPath, [string]$Session, [string]$Project)
$ErrorActionPreference='Stop'
if (-not $PythonPath) {
    $PythonPath=Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/pythonw.exe'
}
if (-not (Test-Path -LiteralPath $PythonPath)) { throw '请用 -PythonPath 指定带 tkinter 的 Python 3.10+ pythonw.exe。' }
$taskScript=Join-Path (Split-Path -Parent $PSScriptRoot) 'src/gui/player_gui.py'
$taskArguments=@(('"'+$taskScript+'"'))
if ($Session) { $taskArguments+=@('--session',('"'+(Resolve-Path -LiteralPath $Session).Path+'"')) }
if ($Project) { $taskArguments+=@('--project',('"'+(Resolve-Path -LiteralPath $Project).Path+'"')) }
Start-Process -FilePath $PythonPath -ArgumentList $taskArguments -WindowStyle Hidden | Out-Null
