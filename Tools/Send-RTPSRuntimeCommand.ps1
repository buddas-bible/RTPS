param(
    [Parameter(Mandatory)]
    [string]$Directory,
    [Parameter(Mandatory)]
    [ValidateSet('host', 'search', 'select', 'join', 'ready', 'start', 'leave', 'leave_to_menu')]
    [string]$Action,
    [string]$LobbyId
)

$ErrorActionPreference = 'Stop'
if ($Action -in @('select', 'join') -and [string]::IsNullOrWhiteSpace($LobbyId)) {
    throw 'Use -LobbyId with select and join.'
}

$folder = (Resolve-Path -LiteralPath $Directory).Path
$id = [guid]::NewGuid().ToString('N')
$pending = Join-Path $folder "command-$id.tmp"
$inbox = Join-Path $folder 'command.json'
$body = @{ id = $id; action = $Action; lobby_id = $LobbyId } | ConvertTo-Json -Compress
try {
    [IO.File]::WriteAllText($pending, $body)
    [IO.File]::Move($pending, $inbox)
    $id
}
finally {
    if (Test-Path -LiteralPath $pending) {
        Remove-Item -LiteralPath $pending
    }
}
