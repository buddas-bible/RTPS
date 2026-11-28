[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{7,40}$')][string]$Commit,
    [Parameter(Mandatory)][string]$VmName,
    [Parameter(Mandatory)][ValidatePattern('^(?!\.{1,2}$)[A-Za-z0-9_.-]+$')][string]$GuestUser,
    [Parameter(Mandatory)][string]$GuestPasswordFile,
    [string]$EngineRoot = 'E:/Game/UE_5.8',
    [string]$OutputDirectory = (Join-Path $env:USERPROFILE 'Documents/RTPS-V'),
    [ValidateRange(1, [int]::MaxValue)][int]$Repeat = 1,
    [string]$VBoxManage = "$env:ProgramFiles\Oracle\VirtualBox\VBoxManage.exe"
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/RTPSGuestDeployment.ps1"
$runId = "$(Get-Date -Format yyyyMMdd-HHmmss)-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
$runDirectory = (New-Item -ItemType Directory -Path (Join-Path $OutputDirectory $runId) -Force).FullName
$report = Join-Path $runDirectory 'result.jsonl'
$worktree = Join-Path $runDirectory 'Source'
$archive = Join-Path $runDirectory 'Package'
$buildLog = Join-Path $runDirectory 'BuildCookRun.log'
$steamOutput = Join-Path $runDirectory 'Steam'
$stage = 'preflight'
$status = 'FAIL'
$message = ''
$sha = $null
$steamReport = $null
$round = 0
$attempt = 0
$firstFailure = $null
$guestDeploymentStarted = $false

function Write-Report([string]$Event, [string]$ResultStatus = '') {
    $record = [ordered]@{
        event = $Event
        utc = [DateTime]::UtcNow.ToString('o')
        run_id = $runId
        commit = $sha
        stage = $stage
        attempt = $attempt
        repeat = $Repeat
        round = $round
        message = $message
        directory = $runDirectory
        steam_report = $steamReport
    }
    if ($ResultStatus) { $record.status = $ResultStatus }
    $line = $record | ConvertTo-Json -Compress
    [IO.File]::AppendAllText($report, $line + [Environment]::NewLine)
    Write-Host $line
}

Write-Report 'test_started'
try {
    $repo = Split-Path -Parent $PSScriptRoot
    $sha = git -C $repo rev-parse --verify --quiet "${Commit}^{commit}"
    if ($LASTEXITCODE -ne 0) { throw "Commit $Commit is unavailable in the local repository." }
    $engine = (Resolve-Path -LiteralPath $EngineRoot).Path
    $runUAT = Join-Path $engine 'Engine/Build/BatchFiles/RunUAT.bat'
    $version = Get-Content -LiteralPath (Join-Path $engine 'Engine/Build/Build.version') -Raw | ConvertFrom-Json
    if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 8 -or !(Test-Path -LiteralPath $runUAT -PathType Leaf)) {
        throw "EngineRoot must point to an Unreal Engine 5.8 installation."
    }
    $passwordFile = (Resolve-Path -LiteralPath $GuestPasswordFile).Path
    Write-Report 'stage_passed'

    $stage = 'checkout'
    $checkoutLog = Join-Path $runDirectory 'Checkout.log'
    git -C $repo worktree add --detach $worktree $sha *> $checkoutLog
    if ($LASTEXITCODE -ne 0) { throw "Git worktree checkout failed (exit $LASTEXITCODE). See $checkoutLog." }
    Write-Report 'stage_passed'

    $stage = 'packaging'
    $project = Join-Path $worktree 'RTPS.uproject'
    & $runUAT BuildCookRun "-project=$project" -target=RTPS -platform=Win64 -clientconfig=Development `
        -build -cook -stage -package -pak -iostore -archive "-archivedirectory=$archive" `
        '-AdditionalCookerOptions=-noshaderworker' '-ubtargs=-NoHotReloadFromIDE -MaxParallelActions=4' -unattended -nop4 *> $buildLog
    $buildExit = $LASTEXITCODE
    if ($buildExit -ne 0) {
        $lastStep = Select-String -LiteralPath $buildLog -Pattern '\*+ (BUILD|COOK|STAGE|PACKAGE|ARCHIVE) COMMAND STARTED' | Select-Object -Last 1
        if ($lastStep) { $stage = $lastStep.Matches[0].Groups[1].Value.ToLowerInvariant() }
        throw "BuildCookRun failed (exit $buildExit). See $buildLog."
    }
    if (!(Test-Path -LiteralPath (Join-Path $archive 'Windows/RTPS.exe') -PathType Leaf)) {
        throw "BuildCookRun did not produce Windows/RTPS.exe. See $buildLog."
    }
    Write-Report 'stage_passed'

    $stage = 'steam/deploy_peer'
    $auth = @("--username=$GuestUser", "--passwordfile=$passwordFile")
    $guestDeploymentStarted = $true
    Publish-GuestBuild (Join-Path $archive 'Windows') $runId
    Write-Report 'guest_build_deployed'

    $steamScript = Join-Path $PSScriptRoot 'Test-RTPSRuntimeSteam.ps1'
    for ($attempt = 1; $attempt -le $Repeat; $attempt++) {
        $stage = 'steam'
        $round = 0
        $message = ''
        $steamReport = $null
        $attemptStatus = 'FAIL'
        $attemptOutput = Join-Path $steamOutput ('{0:D3}' -f $attempt)
        Write-Report 'attempt_started'
        try {
            & (Join-Path $PSHOME 'pwsh.exe') -NoProfile -File $steamScript -Build $archive -VmName $VmName `
                -GuestUser $GuestUser -GuestPasswordFile $passwordFile -VBoxManage $VBoxManage -OutputDirectory $attemptOutput `
                -GuestDeploymentId $runId
            $steamExit = $LASTEXITCODE
            $steamReport = Get-ChildItem -LiteralPath $attemptOutput -Recurse -Filter 'result.jsonl' -File -ErrorAction SilentlyContinue |
                Select-Object -First 1 -ExpandProperty FullName
            if (!$steamReport) { throw "Steam runner produced no result.jsonl (exit $steamExit)." }
            $result = Get-Content -LiteralPath $steamReport -Tail 1 | ConvertFrom-Json
            $round = $result.round
            if ($steamExit -ne 0 -or $result.event -ne 'test_finished' -or $result.status -ne 'PASS' -or $round -ne 2) {
                if ($result.stage) { $stage = "steam/$($result.stage)" }
                throw "Steam verification failed (exit $steamExit). $($result.message)"
            }
            $stage = 'complete'
            $attemptStatus = 'PASS'
        } catch {
            $message = $_.Exception.Message
            if (!$firstFailure) {
                $firstFailure = @{ attempt = $attempt; stage = $stage; round = $round; message = $message; report = $steamReport }
            }
        }
        Write-Report 'attempt_finished' $attemptStatus
    }
    $attempt = $Repeat
    if ($firstFailure) {
        $attempt = $firstFailure.attempt
        $stage = $firstFailure.stage
        $round = $firstFailure.round
        $message = $firstFailure.message
        $steamReport = $firstFailure.report
    } else {
        $status = 'PASS'
    }
} catch {
    $message = $_.Exception.Message
} finally {
    if ($guestDeploymentStarted) {
        try {
            Remove-GuestBuild $runId
            Write-Report 'guest_build_removed'
        } catch {
            $cleanupError = $_.Exception.Message
            if ($status -eq 'PASS') {
                $stage = 'steam/cleanup'
                $status = 'FAIL'
                $message = $cleanupError
            } else {
                $message = "$message Guest build cleanup failed. $cleanupError"
            }
        }
    }
    Write-Report 'test_finished' $status
}

if ($status -eq 'PASS') { exit 0 }
exit 1
