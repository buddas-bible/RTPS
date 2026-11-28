function Invoke-VBox([string[]]$Arguments) {
    $output = & $VBoxManage @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "VBoxManage failed ($LASTEXITCODE): $($output -join ' ')"
    }
    $output
}

function Invoke-Guest([string]$Command) {
    Invoke-VBox (@('guestcontrol', $VmName, 'run', '--exe=C:\Windows\System32\cmd.exe') + $auth +
        @('--timeout=120000', '--wait-stdout', '--wait-stderr', '--', 'cmd.exe', '/c', $Command))
}

function Publish-GuestBuild([string]$Package, [string]$DeploymentId) {
    $path = "C:\Users\$GuestUser\RTPSBuild-$DeploymentId"
    $share = "RTPS-Build-$DeploymentId"
    Invoke-VBox @('sharedfolder', 'add', $VmName, '--name', $share, '--hostpath', $Package, '--readonly', '--transient') | Out-Null
    try {
        Invoke-Guest "mkdir $path" | Out-Null
        Invoke-Guest "robocopy \\VBOXSVR\$share $path /E /XF *.pdb /R:2 /W:2 /NFL /NDL /NJH /NJS & if errorlevel 8 (exit /b 8) else (exit /b 0)" | Out-Null
        foreach ($relative in 'RTPS\Binaries\Win64\RTPS.exe', 'RTPS\Content\Paks\RTPS-Windows.ucas') {
            $expected = (Get-FileHash -LiteralPath (Join-Path $Package $relative)).Hash
            $actual = Invoke-Guest "certutil -hashfile $path\$relative SHA256"
            if ($expected -notin @($actual | ForEach-Object { $_.Trim() })) { throw "Guest build hash mismatch: $relative" }
        }
    } finally {
        Invoke-VBox @('sharedfolder', 'remove', $VmName, '--name', $share, '--transient') | Out-Null
    }
}

function Remove-GuestBuild([string]$DeploymentId) {
    if ($DeploymentId -notmatch '^\d{8}-\d{6}-[0-9a-f]{8}$') { throw 'Invalid guest deployment ID.' }
    $path = "C:\Users\$GuestUser\RTPSBuild-$DeploymentId"
    $command = @'
$ErrorActionPreference = 'Stop'
$path = '@PATH@'
if (Test-Path -LiteralPath $path) {
    $item = Get-Item -LiteralPath $path
    if ($item.FullName -ne $path -or !$item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Unexpected guest deployment directory.'
    }
    Remove-Item -LiteralPath $item.FullName -Recurse -Force
}
'@
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command.Replace('@PATH@', $path)))
    Invoke-Guest "powershell.exe -NoProfile -EncodedCommand $encoded" | Out-Null
}
