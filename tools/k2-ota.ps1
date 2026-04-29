param(
    [string]$Image = "build-h755-ota/K2-Zephyr/zephyr/zephyr.signed.bin",
    [string]$McuIp = "10.77.0.2",
    [int]$McuPort = 1337,
    [double]$Timeout = 10,
    [int]$Tries = 2,
    [int]$PollSeconds = 30,
    [switch]$NoReset
)

$ErrorActionPreference = "Stop"

function Find-Mcumgr {
    if ($env:MCUMGR) {
        return $env:MCUMGR
    }

    $cmd = Get-Command mcumgr -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $userHome = if ($env:USERPROFILE) { $env:USERPROFILE } else { $env:HOME }
    if ($userHome) {
        foreach ($relativePath in @("go\bin\mcumgr.exe", "go/bin/mcumgr")) {
            $goBin = Join-Path $userHome $relativePath
            if (Test-Path $goBin) {
                return $goBin
            }
        }
    }

    throw @"
mcumgr not found. Install it with:
  winget install GoLang.Go
  go install github.com/apache/mynewt-mcumgr-cli/mcumgr@latest
Then make sure %USERPROFILE%\go\bin is on PATH.
"@
}

function Invoke-Mcumgr {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Args)

    $connString = "[$McuIp]:$McuPort"
    $output = & $script:McumgrBin --conntype udp "--connstring=$connString" --timeout $Timeout --tries $Tries @Args 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw ($output -join [Environment]::NewLine)
    }
    return $output -join [Environment]::NewLine
}

function Get-SlotHash {
    param(
        [string]$Text,
        [int]$Slot = 1
    )

    $inSlot = $false
    foreach ($line in ($Text -split "\r?\n")) {
        if ($line -match "^\s*image=\d+\s+slot=$Slot\b") {
            $inSlot = $true
            continue
        }
        if ($line -match "^\s*image=") {
            $inSlot = $false
        }
        if ($inSlot -and $line -match "^\s*hash:\s*([0-9a-fA-F]+)") {
            return $Matches[1]
        }
    }
    return $null
}

$script:McumgrBin = Find-Mcumgr

if (-not (Test-Path $Image)) {
    throw "Image not found: $Image. Build one first, for example: ./build.sh --h7 --ota"
}

Write-Host "MCU: ${McuIp}:${McuPort}"
Write-Host "Image: $Image"
Write-Host ""

Write-Host "Current image state:"
Write-Host (Invoke-Mcumgr image list)
Write-Host ""

Write-Host "Uploading signed image..."
Write-Host (Invoke-Mcumgr image upload $Image)
Write-Host ""

Write-Host "Image state after upload:"
$listAfterUpload = Invoke-Mcumgr image list
Write-Host $listAfterUpload
Write-Host ""

$testHash = Get-SlotHash -Text $listAfterUpload -Slot 1
if (-not $testHash) {
    throw "Could not find slot 1 hash after upload; refusing to reset."
}

Write-Host "Marking slot 1 image for test boot:"
Write-Host "  $testHash"
Write-Host (Invoke-Mcumgr image test $testHash)
Write-Host ""

if ($NoReset) {
    Write-Host "Skipping reset because -NoReset was supplied."
    exit 0
}

Write-Host "Resetting MCU..."
try {
    Write-Host (Invoke-Mcumgr reset)
} catch {
    Write-Warning $_
}
Write-Host ""

Write-Host "Waiting for MCU to come back..."
$deadline = (Get-Date).AddSeconds($PollSeconds)
while ((Get-Date) -lt $deadline) {
    try {
        Write-Host (Invoke-Mcumgr image list)
        exit 0
    } catch {
        Start-Sleep -Seconds 2
    }
}

Write-Warning "MCU did not respond to MCUmgr within ${PollSeconds}s after reset."
Write-Warning "Check power, Ethernet link, and the direct-link network profile."
exit 1
