[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Build,
    [Parameter(Mandatory)][string]$VmName,
    [Parameter(Mandatory)][ValidatePattern('^(?!\.{1,2}$)[A-Za-z0-9_.-]+$')][string]$GuestUser,
    [Parameter(Mandatory)][string]$GuestPasswordFile,
    [string]$VBoxManage = "$env:ProgramFiles\Oracle\VirtualBox\VBoxManage.exe",
    [string]$OutputDirectory = (Join-Path $Build 'Verification'),
    [ValidateRange(10, 600)][int]$TimeoutSeconds = 120,
    [ValidatePattern('^\d{8}-\d{6}-[0-9a-f]{8}$')][string]$GuestDeploymentId
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/RTPSGuestDeployment.ps1"
$runId = "$(Get-Date -Format yyyyMMdd-HHmmss)-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
$runDirectory = (New-Item -ItemType Directory -Path (Join-Path $OutputDirectory $runId) -Force).FullName
$hostDirectory = (New-Item -ItemType Directory -Path (Join-Path $runDirectory 'Host')).FullName
$spool = (New-Item -ItemType Directory -Path (Join-Path $runDirectory 'Spool')).FullName
$report = Join-Path $runDirectory 'result.jsonl'
$peerTrace = Join-Path $runDirectory 'Peer-events.jsonl'
$guestDirectory = "C:\Users\$GuestUser\RTPSRuntime-$runId"
$ownsGuestBuild = !$GuestDeploymentId
if ($ownsGuestBuild) { $GuestDeploymentId = $runId }
$guestBuild = "C:\Users\$GuestUser\RTPSBuild-$GuestDeploymentId"
$task = "RTPS-Steam-$runId"
$hostGame = $null
$guestDeploymentStarted = $false
$taskAdded = $false
$guestPrepared = $false
$hostedSession = $false
$joinedSession = $false
$stage = 'preflight'
$round = 0
$outcome = 'FAIL'
$failure = ''

function Write-Report([string]$Event, [string]$Message = '') {
    $record = [ordered]@{
        event = $Event
        utc = [DateTime]::UtcNow.ToString('o')
        run_id = $runId
        stage = $stage
        round = $round
        message = $Message
    }
    if ($Event -eq 'test_finished') {
        $record.status = $outcome
        $record.directory = $runDirectory
    }
    $line = $record | ConvertTo-Json -Compress
    [IO.File]::AppendAllText($report, $line + [Environment]::NewLine)
    Write-Host $line
}

function Copy-GuestFile([string]$Source, [string]$Destination) {
    Invoke-VBox (@('guestcontrol', $VmName, 'copyfrom') + $auth + @($Source, $Destination)) | Out-Null
}

function Send-GuestFile([string]$Source, [string]$Destination) {
    Invoke-VBox (@('guestcontrol', $VmName, 'copyto') + $auth + @($Source, $Destination)) | Out-Null
}

function Copy-PeerTrace {
    # VBoxManage copies the snapshot without holding a handle on the live trace.
    Invoke-Guest "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $guestDirectory\Launch-Peer.ps1 -Snapshot" | Out-Null
    Copy-GuestFile "$guestDirectory\events-snapshot.jsonl" $peerTrace
}

function Read-Trace([string]$Side) {
    $path = Join-Path $hostDirectory 'events.jsonl'
    if ($Side -eq 'Peer') {
        try { Copy-PeerTrace } catch { return }
        $path = $peerTrace
    }
    if (Test-Path -LiteralPath $path) {
        foreach ($line in (Get-Content -LiteralPath $path -Encoding UTF8)) {
            if ($line) { try { ConvertFrom-Json -InputObject $line } catch {} }
        }
    }
}

function Wait-Trace([string]$Side, [string]$Event, [scriptblock]$Where = { $true },
    [int]$Count = 1, [int]$Seconds = $TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        if ($hostGame -and $hostGame.HasExited) { throw "Host exited ($($hostGame.ExitCode))" }
        $matches = @(Read-Trace $Side | Where-Object { $_.event -eq $Event -and (& $Where $_) })
        if ($matches.Count -ge $Count) { return $matches[-1] }
        Start-Sleep -Seconds 1
    } while ((Get-Date) -lt $deadline)
    throw "Timed out waiting for $Side $Event (expected $Count)"
}

function Send-Command([string]$Side, [string]$Action, [string]$LobbyId = '') {
    $location = if ($Side -eq 'Peer') { $spool } else { $hostDirectory }
    $request = & "$PSScriptRoot/Send-RTPSRuntimeCommand.ps1" -Directory $location -Action $Action -LobbyId $LobbyId
    if ($Side -eq 'Peer') {
        $file = Join-Path $location 'command.json'
        Send-GuestFile $file "$guestDirectory\incoming.json"
        Remove-Item -LiteralPath $file
        Invoke-Guest "if exist $guestDirectory\command.json (exit /b 3) else (move $guestDirectory\incoming.json $guestDirectory\command.json)" | Out-Null
    }
    $reply = Wait-Trace $Side 'command_finished' { param($entry) $entry.id -eq $request }
    if ($reply.status -ne 'accepted') { throw "$Side rejected $Action" }
}

function Close-Session([string]$Side) {
    Send-Command $Side leave
    $null = Wait-Trace $Side 'session_destroyed' { param($e) $e.success -eq 'true' }
}

try {
    Write-Report 'test_started'
    $auth = @("--username=$GuestUser", "--passwordfile=$((Resolve-Path -LiteralPath $GuestPasswordFile).Path)")
    $package = (Resolve-Path -LiteralPath (Join-Path $Build 'Windows')).Path
    $exe = (Resolve-Path -LiteralPath "$package/RTPS/Binaries/Win64/RTPS.exe").Path
    $manifest = 'RTPS/Content/Paks/RTPS-Windows.ucas'
    $null = Get-Item -LiteralPath "$package/$manifest"
    $info = Invoke-VBox @('showvminfo', $VmName, '--machinereadable')
    if ($info -notcontains 'VMState="running"') { throw 'The existing VM must be running with Steam signed in.' }

    $stage = 'deploy_peer'
    if ($ownsGuestBuild) {
        $guestDeploymentStarted = $true
        Publish-GuestBuild $package $GuestDeploymentId
    }
    Invoke-Guest "mkdir $guestDirectory" | Out-Null
    $guestPrepared = $true

    $stage = 'launch'
    $launcher = Join-Path $spool 'Launch-Peer.ps1'
    $launcherBody = @'
param([switch]$Snapshot)

$ErrorActionPreference = 'Stop'
if ($Snapshot) {
    $source = [IO.File]::Open('@RUN@\events.jsonl', 'Open', 'Read', 'ReadWrite')
    try {
        $target = [IO.File]::Create('@RUN@\events-snapshot.jsonl')
        try { $source.CopyTo($target) } finally { $target.Dispose() }
    } finally {
        $source.Dispose()
    }
    exit 0
}

$exe = '@BUILD@\RTPS\Binaries\Win64\RTPS.exe'
$p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru -WindowStyle Hidden -ArgumentList @(
    '-nullrhi', '-nosound', '-unattended', '-NoSplash', '-NoCrashDialog', '-ExecCmds="t.MaxFPS 60"',
    '-RTPSRuntimeTestDir=@RUN@', '-RTPSLobbyTestRun=@RUN_ID@', '-abslog=@RUN@\Peer.log')
Set-Content -LiteralPath '@RUN@\peer.pid' -Value $p.Id
$p.WaitForExit()
'@
    $launcherBody.Replace('@BUILD@', $guestBuild).Replace('@RUN@', $guestDirectory).Replace('@RUN_ID@', $runId) |
        Set-Content -LiteralPath $launcher -Encoding UTF8
    Send-GuestFile $launcher "$guestDirectory\Launch-Peer.ps1"
    $taskSetup = Join-Path $spool 'Start-Peer.cmd'
    @"
@echo off
schtasks /Create /TN $task /SC ONCE /ST 00:00 /TR "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File $guestDirectory\Launch-Peer.ps1" /RU $GuestUser /IT /F
if errorlevel 1 exit /b 1
schtasks /Run /TN $task
"@ | Set-Content -LiteralPath $taskSetup -Encoding ascii
    Send-GuestFile $taskSetup "$guestDirectory\Start-Peer.cmd"
    $arguments = @('-nullrhi', '-nosound', '-unattended', '-NoSplash', '-NoCrashDialog', '-ExecCmds="t.MaxFPS 60"',
        ('-abslog="' + $hostDirectory + '/Host.log"'), ('-RTPSRuntimeTestDir="' + $hostDirectory + '"'),
        "-RTPSLobbyTestRun=$runId",
        '-RTPSAutoQuestId=voxel_arena', '-RTPSAutoArenaDemo')
    $hostGame = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
    $taskAdded = $true
    Invoke-Guest "$guestDirectory\Start-Peer.cmd" | Out-Null
    foreach ($side in 'Host', 'Peer') {
        $null = Wait-Trace $side 'online_subsystem' { param($e) $e.name -eq 'STEAM' }
        $null = Wait-Trace $side 'map_loaded' { param($e) $e.map -eq 'MainMenuMap' }
    }

    $stage = 'host'
    Send-Command Host host
    $created = Wait-Trace Host 'host_created' { param($e) $e.success -eq 'true' -and $e.lobby_id }
    $hostedSession = $true
    $lobbyId = $created.lobby_id
    $stage = 'search_and_row'
    for ($attempt = 1; $attempt -le 4; $attempt++) {
        Send-Command Peer search
        $completed = Wait-Trace Peer 'search_completed' { $true } $attempt
        if ($completed.success -ne 'true') {
            if ($attempt -eq 4) { throw "Steam lobby search returned success:false on attempt $attempt." }
            Write-Report 'search_retry' "Steam lobby search returned success:false on attempt $attempt."
            Start-Sleep -Seconds 5
            continue
        }
        try {
            $row = Wait-Trace Peer 'session_row_added' { param($e) $e.lobby_id -eq $lobbyId } 1 5
            break
        } catch {
            if ($attempt -eq 4) { throw }
        }
    }
    Write-Report 'stage_passed' "UMG row: $lobbyId, $($row.players)/$($row.capacity)"

    $stage = 'select'
    Send-Command Peer select $lobbyId
    $null = Wait-Trace Peer 'row_selected' { param($e) $e.lobby_id -eq $lobbyId }
    $stage = 'join'
    Send-Command Peer join $lobbyId
    $joined = Wait-Trace Peer 'join_completed'
    if ($joined.result -ne '0') { throw "Steam join returned $($joined.result)" }
    $joinedSession = $true
    $null = Wait-Trace Peer 'join_address' { param($e) $e.address -like 'steam.*' }
    $null = Wait-Trace Peer 'map_loaded' { param($e) $e.map -eq 'CharacterTestLevel' }
    if (!(Select-String -LiteralPath "$hostDirectory/Host.log" -SimpleMatch 'SteamSockets: New connection' -Quiet)) {
        throw 'Host log did not confirm SteamSockets transport'
    }
    Write-Report 'stage_passed' 'Peer entered the lobby over SteamSockets'

    for ($round = 1; $round -le 2; $round++) {
        $stage = 'ready'
        $null = Wait-Trace Host 'lobby_state' { param($e) $e.connected -eq '2' -and $e.ready -eq '0' } $round
        $before = @(Read-Trace Host | Where-Object { $_.event -eq 'lobby_state' -and $_.connected -eq '2' -and $_.ready -eq '2' }).Count
        Send-Command Host ready
        Send-Command Peer ready
        $null = Wait-Trace Host 'lobby_state' { param($e) $e.connected -eq '2' -and $e.ready -eq '2' } ($before + 1)
        $stage = 'start'
        Send-Command Host start
        $null = Wait-Trace Host 'round_started' { $true } $round
        $null = Wait-Trace Peer 'map_loaded' { param($e) $e.map -eq 'ArenaMap' } $round
        $stage = 'result'
        $hostResult = Wait-Trace Host 'result_received' { param($e) $e.result -eq 'Victory' } $round
        $peerResult = Wait-Trace Peer 'result_received' { param($e) $e.result -eq 'Defeat' } $round
        if ($hostResult.winner_team -ne $peerResult.winner_team) { throw 'Host and peer recorded different winners' }
        $stage = 'return'
        $null = Wait-Trace Host 'return_to_lobby' { param($e) $e.started -eq 'true' } $round
        foreach ($side in 'Host', 'Peer') {
            $null = Wait-Trace $side 'map_loaded' { param($e) $e.map -eq 'CharacterTestLevel' } ($round + 1)
        }
        Write-Report 'stage_passed' 'Victory/Defeat recorded and both players returned'
    }
    $stage = 'leave'
    Close-Session Peer
    $joinedSession = $false
    Close-Session Host
    $hostedSession = $false
    $stage = 'complete'
    $round = 2
    $outcome = 'PASS'
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($joinedSession) { try { Close-Session Peer } catch {} }
    if ($hostedSession) { try { Close-Session Host } catch {} }
    if ($hostGame -and !$hostGame.HasExited) { Stop-Process -InputObject $hostGame -ErrorAction SilentlyContinue }
    if ($guestPrepared) {
        try { Copy-PeerTrace } catch {}
        try { Copy-GuestFile "$guestDirectory\Peer.log" "$runDirectory/Peer.log" } catch {}
    }
    if ($taskAdded) {
        try {
            $pidFile = Join-Path $spool 'peer.pid'
            Copy-GuestFile "$guestDirectory\peer.pid" $pidFile
            $peerId = [int](Get-Content -LiteralPath $pidFile -Raw)
            Invoke-Guest "taskkill /PID $peerId /F" | Out-Null
        } catch {}
        try { Invoke-Guest "schtasks /Delete /TN $task /F" | Out-Null } catch {}
    }
    try {
        if ($guestPrepared) {
            Invoke-Guest "del /Q $guestDirectory\Launch-Peer.ps1 $guestDirectory\Start-Peer.cmd $guestDirectory\peer.pid $guestDirectory\command.json $guestDirectory\incoming.json $guestDirectory\events-snapshot.jsonl" | Out-Null
        }
    } catch {
        Write-Report 'cleanup_failed' $_.Exception.Message
        if ($outcome -eq 'PASS') { $stage = 'cleanup'; $outcome = 'FAIL'; $failure = $_.Exception.Message }
    }
    if ($guestDeploymentStarted) {
        try { Remove-GuestBuild $GuestDeploymentId } catch {
            Write-Report 'cleanup_failed' $_.Exception.Message
            if ($outcome -eq 'PASS') { $stage = 'cleanup'; $outcome = 'FAIL'; $failure = $_.Exception.Message }
        }
    }
    Write-Report 'test_finished' $failure
}
if ($outcome -ne 'PASS') { exit 1 }
exit 0
