param([switch]$Test, [string]$OutputDirectory, [switch]$RuntimeProbe, [switch]$PoseRoundTrip, [switch]$SingleTrackPlayback, [switch]$FreezeTime, [switch]$AnimatorProbe, [switch]$AnimatorToggle, [switch]$AnimatorLease, [switch]$MultiTrackPlayback, [switch]$PlayerSession, [switch]$BindPoseProbe, [switch]$FullBodyFK, [switch]$RootMotion, [switch]$LegIK, [switch]$ToeIK, [switch]$CameraRoundTrip, [switch]$CameraPlayback)
$ErrorActionPreference = 'Stop'
if ($CameraPlayback -and (-not $PlayerSession -or $CameraRoundTrip)) { throw '-CameraPlayback requires -PlayerSession and excludes -CameraRoundTrip.' }
if ($CameraRoundTrip -and (-not $RuntimeProbe -or $SingleTrackPlayback -or $PoseRoundTrip -or $AnimatorToggle)) { throw '-CameraRoundTrip requires a read-only -RuntimeProbe build.' }
if ($PoseRoundTrip -and -not $RuntimeProbe) { throw '-PoseRoundTrip requires -RuntimeProbe.' }
if ($SingleTrackPlayback -and (-not $RuntimeProbe -or $PoseRoundTrip)) { throw '-SingleTrackPlayback requires -RuntimeProbe and excludes -PoseRoundTrip.' }
if ($FreezeTime -and -not $SingleTrackPlayback) { throw '-FreezeTime requires -SingleTrackPlayback.' }
if ($AnimatorProbe -and (-not $RuntimeProbe -or ($SingleTrackPlayback -and -not $AnimatorLease) -or $PoseRoundTrip)) { throw '-AnimatorProbe requires read-only -RuntimeProbe or -AnimatorLease.' }
if ($AnimatorToggle -and -not $AnimatorProbe) { throw '-AnimatorToggle requires -AnimatorProbe.' }
if ($AnimatorLease -and (-not $SingleTrackPlayback -or $FreezeTime -or $AnimatorToggle)) { throw '-AnimatorLease requires -SingleTrackPlayback and excludes -FreezeTime / -AnimatorToggle.' }
if ($MultiTrackPlayback -and -not $AnimatorLease) { throw '-MultiTrackPlayback requires -AnimatorLease.' }
if ($PlayerSession -and -not $MultiTrackPlayback) { throw '-PlayerSession requires -MultiTrackPlayback.' }
if ($BindPoseProbe -and -not ($AnimatorProbe -or $AnimatorLease)) { throw '-BindPoseProbe requires -AnimatorProbe or -AnimatorLease.' }
if ($FullBodyFK -and -not $MultiTrackPlayback) { throw '-FullBodyFK requires -MultiTrackPlayback.' }
if ($ToeIK -and -not $LegIK) { throw '-ToeIK requires -LegIK.' }
if ($LegIK -and -not $RootMotion) { throw '-LegIK requires -RootMotion.' }
if ($RootMotion -and -not $FullBodyFK) { throw '-RootMotion requires -FullBodyFK.' }
$taskRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere.exe is required.' }
$taskVs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $taskVs) { throw 'Visual Studio C++ x64 tools are required.' }
& (Join-Path $taskVs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$taskBuild = if($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $taskRoot 'build' }
New-Item -ItemType Directory -Path $taskBuild -Force | Out-Null
Push-Location $taskBuild
try {
    $common = @('/nologo','/std:c++20','/EHsc','/W4','/WX','/utf-8','/MD','/Zi',
        "/I$taskRoot/src", "/I$taskRoot/third_party/better_endfield")
    if ($PoseRoundTrip) { $common += '/DGIMMD_POSE_ROUNDTRIP' }
    if ($SingleTrackPlayback) { $common += '/DGIMMD_SINGLE_TRACK_PLAYBACK' }
    if ($FreezeTime) { $common += '/DGIMMD_FREEZE_TIME' }
    if ($AnimatorProbe -or $AnimatorLease) { $common += '/DGIMMD_ANIMATOR_PROBE' }
    if ($AnimatorToggle) { $common += '/DGIMMD_ANIMATOR_TOGGLE' }
    if ($AnimatorLease) { $common += '/DGIMMD_ANIMATOR_LEASE' }
    if ($MultiTrackPlayback) { $common += '/DGIMMD_MULTI_TRACK_PLAYBACK' }
    if ($PlayerSession) { $common += '/DGIMMD_PLAYER_SESSION' }
    if ($BindPoseProbe) { $common += '/DGIMMD_BIND_POSE_PROBE' }
    if ($FullBodyFK) { $common += '/DGIMMD_FULL_BODY_FK'; if (-not $BindPoseProbe) { $common += '/DGIMMD_BIND_POSE_PROBE' } }
    if ($RootMotion) { $common += '/DGIMMD_ROOT_MOTION' }
    if ($LegIK) { $common += '/DGIMMD_LEG_IK' }
    if ($ToeIK) { $common += '/DGIMMD_TOE_IK' }
    if ($CameraRoundTrip) { $common += '/DGIMMD_CAMERA_ROUNDTRIP' }
    if ($CameraPlayback) { $common += '/DGIMMD_CAMERA_PLAYBACK' }
    $probeSource = 'src/windows/probe.cpp'
    $probeObjects = @()
    if ($RuntimeProbe) {
        if (-not (Test-Path -LiteralPath (Join-Path $taskRoot 'src/generated/gi_profile.h'))) { throw 'Generate a pinned GI profile first.' }
        $probeSource = 'src/windows/runtime_probe.cpp'
        foreach ($sourceFile in @('src/buffer.c','src/hook.c','src/trampoline.c','src/hde/hde64.c')) {
            $sourcePath = Join-Path $taskRoot "third_party/minhook/$sourceFile"
            & cl.exe /nologo /c /O2 /MD /W3 "/I$taskRoot/third_party/minhook/include" $sourcePath
            if ($LASTEXITCODE -ne 0) { throw "MinHook build failed: $sourceFile" }
            $probeObjects += ([IO.Path]::GetFileNameWithoutExtension($sourceFile) + '.obj')
        }
    }
    $targets = @(
        @{Source='src/tools/vmd_inspect.cpp'; Output='gimmd-vmd.exe'; Dll=$false},
        @{Source='src/tools/ik_inspect.cpp'; Output='gimmd-ik-inspect.exe'; Dll=$false},
        @{Source='src/tools/camera_inspect.cpp'; Output='gimmd-camera.exe'; Dll=$false},
        @{Source=$probeSource; Output='gimmd-probe.dll'; Dll=$true},
        @{Source='src/windows/probe_host.cpp'; Output='gimmd-probe-host.exe'; Dll=$false},
        @{Source='src/windows/inject_probe.cpp'; Output='gimmd-inject-probe.exe'; Dll=$false},
        @{Source='tests/core_tests.cpp'; Output='gimmd-tests.exe'; Dll=$false}
    )
    foreach ($target in $targets) {
        $argsForCompiler = $common + @((Join-Path $taskRoot $target.Source), "/Fe:$($target.Output)")
        if ($target.Dll) { $argsForCompiler += @('/LD', "/I$taskRoot/third_party/minhook/include") + $probeObjects }
        & cl.exe @argsForCompiler
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $($target.Output)" }
    }
    if ($RuntimeProbe) {
        & cl.exe @common /DGIMMD_OBSERVER_TEST "/I$taskRoot/third_party/minhook/include" (Join-Path $taskRoot 'src/windows/runtime_probe.cpp') @probeObjects /Fe:gimmd-runtime-tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Runtime observer test build failed.' }
    }
    $taskManifest = [ordered]@{
        schemaVersion=1; playerSession=[bool]$PlayerSession; fullBodyFK=[bool]$FullBodyFK
        cameraPlayback=[bool]$CameraPlayback; rootMotion=[bool]$RootMotion; legIK=[bool]$LegIK; toeIK=[bool]$ToeIK
        dllSha256=(Get-FileHash -LiteralPath './gimmd-probe.dll' -Algorithm SHA256).Hash
        loaderSha256=(Get-FileHash -LiteralPath './gimmd-inject-probe.exe' -Algorithm SHA256).Hash
    }
    $taskManifest | ConvertTo-Json | Set-Content -LiteralPath './build-manifest.json' -Encoding utf8
    if ($Test) {
        & './gimmd-tests.exe' (Join-Path $taskBuild 'synthetic.vmd')
        if ($LASTEXITCODE -ne 0) { throw 'Core tests failed.' }
        $json = & './gimmd-vmd.exe' (Join-Path $taskBuild 'synthetic.vmd') '0.5'
        if ($LASTEXITCODE -ne 0) { throw 'VMD CLI failed.' }
        $summary = $json | ConvertFrom-Json
        if ($summary.bones[0].position[0] -ne 6) { throw 'CLI sample mismatch.' }
        & './gimmd-probe-host.exe' --load (Join-Path $taskBuild 'gimmd-probe.dll')
        if ($LASTEXITCODE -ne 0) { throw 'In-process probe test failed.' }
        if ($RuntimeProbe) {
            & './gimmd-runtime-tests.exe'
            if ($LASTEXITCODE -ne 0) { throw 'Runtime observer tests failed.' }
            $hostReport = Get-ChildItem -LiteralPath $taskBuild -Filter '*.probe.json' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
            $hostStatus = Get-Content -LiteralPath $hostReport.FullName -Raw | ConvertFrom-Json
            if ($hostStatus.buildMatch -or $hostStatus.hookInstalled -or $hostStatus.status -ne 'build-mismatch') { throw 'Unknown-build rejection failed.' }
        }
        Write-Output 'Core, CLI and in-process probe checks passed.'
    }
} finally { Pop-Location }

