param(
    [Parameter(Mandatory = $true)][string]$OwnerFile,
    [Parameter(Mandatory = $true)][string]$RecordFile,
    [Parameter(Mandatory = $true)][string]$StatusFile,
    [Parameter(Mandatory = $true)][string]$BatchFile,
    [Parameter(Mandatory = $true)][string]$WorkingDirectory,
    [Parameter(Mandatory = $true)][string]$StdoutFile,
    [Parameter(Mandatory = $true)][string]$StderrFile,
    [Parameter(Mandatory = $true)][string]$RunId,
    [Parameter(Mandatory = $true)][string]$Nonce,
    [Parameter(Mandatory = $true)][string]$Tag,
    [Parameter(Mandatory = $true)][ValidateSet('pc')][string]$Role,
    [Parameter(Mandatory = $true)][ValidateSet('pub', 'sub')][string]$Mode,
    [Parameter(Mandatory = $true)][ValidateSet('pc_to_b', 'b_to_pc')][string]$Direction,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_-]{1,160}$')][string]$Token,
    [Parameter(Mandatory = $true)][ValidatePattern('^(10)$')][string]$Count,
    [Parameter(Mandatory = $true)][ValidatePattern('^(512)$')][string]$PayloadBytes
)

$ErrorActionPreference = 'Stop'

function Write-Status([int]$Code) {
    $line = "D0_PC_EXIT RUN_ID=$RunId NONCE=$Nonce TAG=$Tag RC=$Code"
    $stream = [System.IO.File]::Open($StatusFile, [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes("$line$([Environment]::NewLine)")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
    } finally {
        $stream.Dispose()
    }
}

try {
    $ownerExpected = "D0_PC_RUN_OWNER RUN_ID=$RunId NONCE=$Nonce"
    if (-not (Test-Path -LiteralPath $OwnerFile) -or
        ([System.IO.File]::ReadAllText($OwnerFile).Trim()) -ne $ownerExpected) {
        throw 'run owner file is absent or does not match this invocation'
    }
    foreach ($path in @($RecordFile, $StatusFile, $StdoutFile, $StderrFile)) {
        if (Test-Path -LiteralPath $path) {
            throw "refusing to reuse existing run-scoped path: $path"
        }
    }
    if (-not (Test-Path -LiteralPath $BatchFile) -or
        -not (Test-Path -LiteralPath $WorkingDirectory)) {
        throw 'PC batch file or working directory is absent'
    }

    $self = Get-Process -Id $PID -ErrorAction Stop
    $start = $self.StartTime.ToUniversalTime().ToFileTimeUtc()
    $record = "D0_PC_RECORD RUN_ID=$RunId NONCE=$Nonce TAG=$Tag PID=$PID START=$start"
    $recordStream = [System.IO.File]::Open($RecordFile, [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $recordBytes = [System.Text.Encoding]::UTF8.GetBytes("$record$([Environment]::NewLine)")
        $recordStream.Write($recordBytes, 0, $recordBytes.Length)
        $recordStream.Flush()
    } finally {
        $recordStream.Dispose()
    }
    if (([System.IO.File]::ReadAllText($RecordFile).Trim()) -ne $record) {
        throw 'PC guard record verification failed'
    }

    # The batch accepts no command-line input.  These validated, child-only
    # environment fields are the only dynamic probe inputs it consumes.
    $env:MDDS_D0_ROLE = $Role
    $env:MDDS_D0_MODE = $Mode
    $env:MDDS_D0_DIRECTION = $Direction
    $env:MDDS_D0_TOKEN = $Token
    $env:MDDS_D0_COUNT = $Count
    $env:MDDS_D0_PAYLOAD_BYTES = $PayloadBytes
    $child = Start-Process -FilePath $env:ComSpec -ArgumentList @('/d', '/s', '/c', "call $BatchFile") -WorkingDirectory $WorkingDirectory -RedirectStandardOutput $StdoutFile -RedirectStandardError $StderrFile -WindowStyle Hidden -PassThru
    $child.WaitForExit()
    $child.Refresh()
    Write-Status ([int]$child.ExitCode)
    exit $child.ExitCode
} catch {
    try { Write-Status 70 } catch { }
    Write-Error $_
    exit 70
}
