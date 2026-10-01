param(
    [ValidateSet('Inspect','Install','Create','Remove','Test')][string]$Action = 'Inspect',
    [string]$SetupPath,
    [Parameter(Mandatory=$true)][string]$StateDirectory
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $StateDirectory | Out-Null
$resultPath = Join-Path $StateDirectory 'result.json'
$ownershipPath = Join-Path $StateDirectory 'owned.json'
$lock = $null
function Invoke-Setup([string[]]$Arguments) {
    $output = & $SetupPath @Arguments 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "setupc exit $LASTEXITCODE : $output" }
    return $output
}
function Read-Pairs {
    $text = Invoke-Setup @('list')
    $pairs = @{}
    foreach ($line in ($text -split "`n")) {
        if ($line -match 'CNC([AB])(\d+)\s+(.*)') {
            $side = $Matches[1]; $id = $Matches[2]; $details = $Matches[3]
            $port = ''
            if ($details -match 'RealPortName=(COM\d+)') { $port = $Matches[1] }
            elseif ($details -match 'PortName=(COM\d+)') { $port = $Matches[1] }
            if (-not $pairs.ContainsKey($id)) { $pairs[$id] = @{ Id=$id; A=''; B='' } }
            $pairs[$id][$side] = $port
        }
    }
    return $pairs
}
function Save-Owned($owned) {
    $temp = "$ownershipPath.tmp"
    ConvertTo-Json -Depth 5 -InputObject @($owned) | Set-Content -Encoding UTF8 $temp
    Move-Item -Force $temp $ownershipPath
}
try {
    $lock = [IO.File]::Open((Join-Path $StateDirectory 'operation.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    if ($Action -eq 'Install') {
        # Pinned official distribution. Never disable signature enforcement.
        $archive = Join-Path $StateDirectory 'com0com-3.0.0.0.zip'
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri 'https://downloads.sourceforge.net/project/com0com/com0com/3.0.0.0/com0com-3.0.0.0-i386-and-x64-signed.zip' -OutFile $archive
        if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne '6e5d4359865277430d4ae88c73fb7e648a0ed8e81aea5002478179cfcb0bb0e1') { throw 'Driver archive hash mismatch; refusing execution.' }
        $extract = Join-Path $StateDirectory 'driver-installer'
        Expand-Archive -Force -LiteralPath $archive -DestinationPath $extract
        $installer = Join-Path $extract 'Setup_com0com_v3.0.0.0_W7_x64_signed.exe'
        if ((Get-FileHash $installer -Algorithm SHA256).Hash -ne '26486b28604b49a9008c54feb11b9ece0008a8287ee5caf0bcf2a62f4317128f') { throw 'Installer hash mismatch.' }
        if ((Get-AuthenticodeSignature $installer).Status -ne 'Valid') { throw 'Installer signature is not trusted on this machine.' }
        # Interactive official installer preserves license and driver security prompts.
        $process = Start-Process -FilePath $installer -PassThru -Wait
        if ($process.ExitCode -ne 0) { throw "Installer exited $($process.ExitCode); inspect installation before creating pairs." }
    }
    if (-not $SetupPath) {
        foreach ($candidate in @("$env:ProgramFiles\com0com\setupc.exe", "${env:ProgramFiles(x86)}\com0com\setupc.exe")) {
            if (Test-Path -LiteralPath $candidate) { $SetupPath = $candidate; break }
        }
    }
    if (-not $SetupPath -or -not (Test-Path -LiteralPath $SetupPath)) {
        throw 'com0com setupc.exe not found. Install a compatible driver first; Windows security settings will not be changed.'
    }
    $pairs = Read-Pairs
    $owned = @()
    if (Test-Path $ownershipPath) { $owned = @(Get-Content -Raw $ownershipPath | ConvertFrom-Json) }
    # Never adopt unrelated existing pairs, and never remove a changed pair.
    foreach ($pair in $owned) {
        if ($pairs.ContainsKey([string]$pair.Id)) {
            $actual = $pairs[[string]$pair.Id]
            if ($actual.A -ne $pair.A -or $actual.B -ne $pair.B) {
                throw "Ownership conflict for pair $($pair.Id); refusing changes."
            }
        }
    }
    if ($Action -eq 'Remove') {
        foreach ($pair in @($owned)) {
            if ($pairs.ContainsKey([string]$pair.Id)) {
                Invoke-Setup @('remove', [string]$pair.Id) | Out-Null
                $pairs = Read-Pairs
                if ($pairs.ContainsKey([string]$pair.Id)) { throw "Pair $($pair.Id) was not removed" }
            }
            $owned = @($owned | Where-Object { $_.Id -ne $pair.Id })
            Save-Owned $owned
        }
    } elseif ($Action -eq 'Create') {
        $owned = @($owned | Where-Object { $pairs.ContainsKey([string]$_.Id) })
        Save-Owned $owned
        while ($owned.Count -lt 2) {
            $id = 0
            while ($pairs.ContainsKey([string]$id)) { $id++ }
            # COM# delegates allocation to Windows rather than guessing free numbers.
            Invoke-Setup @('install', [string]$id, 'PortName=COM#', 'PortName=COM#') | Out-Null
            $pairs = Read-Pairs
            $created = $pairs[[string]$id]
            if (-not $created -or -not $created.A -or -not $created.B) {
                throw "Pair $id did not expose two COM names. Inspect com0com before retrying."
            }
            $owned += [pscustomobject]$created
            Save-Owned $owned
        }
    }
    $ports = @([IO.Ports.SerialPort]::GetPortNames())
    $ready = $owned.Count -eq 2
    foreach ($pair in $owned) {
        if (-not $pairs.ContainsKey([string]$pair.Id) -or $ports -notcontains $pair.A -or $ports -notcontains $pair.B) { $ready=$false }
    }
    $tested = $false
    if ($Action -eq 'Test') {
        if (-not $ready) { throw 'Two owned, present pairs are required for self-test.' }
        foreach ($pair in $owned) {
            $a = New-Object IO.Ports.SerialPort($pair.A,38400,'None',8,'One')
            $b = New-Object IO.Ports.SerialPort($pair.B,38400,'None',8,'One')
            try {
                $a.ReadTimeout=1000; $b.ReadTimeout=1000
                $a.WriteTimeout=1000; $b.WriteTimeout=1000
                $a.Open(); $b.Open()
                $a.DiscardInBuffer(); $b.DiscardInBuffer()
                $packet = ':000011 -0355G-0053  0129' + "`r`n"
                $a.Write($packet)
                $received=''
                while ($received.Length -lt $packet.Length) { $received += [char]$b.ReadChar() }
                if ($received -cne $packet) { throw 'Forward pair bytes mismatch' }
                $b.Write($packet); $received=''
                while ($received.Length -lt $packet.Length) { $received += [char]$a.ReadChar() }
                if ($received -cne $packet) { throw 'Reverse pair bytes mismatch' }
            } finally { $a.Dispose(); $b.Dispose() }
        }
        $tested=$true
    }
    $result = @{ Ok=$true; Ready=$ready; ConnectivityTested=$tested; Setup=$SetupPath; Pairs=@($owned); Message='Pair enumeration is not a proxy end-to-end test.' }
    ConvertTo-Json -Depth 6 $result | Set-Content -Encoding UTF8 $resultPath
    # Simple machine-readable mapping alongside the human-readable JSON.
    $mapping = Join-Path $StateDirectory 'ports.txt'
    if ($ready) { @($owned[0].A, $owned[0].B, $owned[1].A, $owned[1].B) | Set-Content -Encoding ASCII $mapping }
    elseif (Test-Path $mapping) { Remove-Item $mapping }
} catch {
    @{ Ok=$false; Ready=$false; Message=$_.Exception.Message } | ConvertTo-Json | Set-Content -Encoding UTF8 $resultPath
    exit 1
} finally {
    if ($lock) { $lock.Dispose() }
}