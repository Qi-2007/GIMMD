param([switch]$ReadOnly, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskOptions = @{
    Test = $true
    RuntimeProbe = $true
    OutputDirectory = Join-Path $taskRoot 'build-studio-7.2-face'
}
if ($OutputDirectory) { $taskOptions.OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory) }
if ($ReadOnly) {
    $taskOptions.OutputDirectory = Join-Path $taskRoot 'build-observer-7.2'
    $taskOptions.AnimatorProbe = $true
    $taskOptions.BindPoseProbe = $true
} else {
    foreach ($taskFeature in @('SingleTrackPlayback', 'AnimatorLease', 'MultiTrackPlayback',
        'PlayerSession', 'FullBodyFK', 'RootMotion', 'LegIK', 'ToeIK', 'CameraPlayback', 'LocalActors', 'SdkPrefabs')) {
        $taskOptions[$taskFeature] = $true
    }
}
& (Join-Path $PSScriptRoot 'build.ps1') @taskOptions
