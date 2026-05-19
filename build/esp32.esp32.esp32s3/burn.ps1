# burn.ps1 - ESP32 OTA Firmware Upload Script
#Requires -Version 5.1

[CmdletBinding()]
param(
    [switch]$DebugMode,
    [SecureString]$Password
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

# --- Configuration ---
$BuildDir = $PSScriptRoot
$ProjectDir = Split-Path -Parent $BuildDir
$ConfigFile = Join-Path $ProjectDir "..\Config.h"
$FirmwareName = "Weather_Station.ino.bin"
$Arduino15Path = "$env:LOCALAPPDATA\Arduino15\packages\esp32"

# --- Helper Functions ---
function Write-Status {
    param([string]$Message, [string]$Type = "Info")
    $colors = @{ Error = "Red"; Warning = "Yellow"; Success = "Green"; Info = "Cyan" }
    Write-Host "[$Type] $Message" -ForegroundColor $colors[$Type]
}

function Get-ConfigValue {
    param([string]$Content, [string]$Name, [switch]$AsNumber)

    $quotedPattern = '#define\s+' + [regex]::Escape($Name) + '\s+"([^"]+)"'
    if ($Content -match $quotedPattern) {
        return $AsNumber ? [int]$Matches[1] : $Matches[1]
    }

    $unquotedPattern = '#define\s+' + [regex]::Escape($Name) + '\s+([^\s]+)'
    if ($Content -match $unquotedPattern) {
        return $AsNumber ? [int]$Matches[1] : $Matches[1]
    }

    throw "Configuration '$Name' not found"
}

function Get-Password {
    param([SecureString]$ProvidedPassword)

    if ($ProvidedPassword) {
        Write-Status "Using provided password" "Warning"
        return $ProvidedPassword
    }

    return Read-Host "> Enter OTA password" -AsSecureString
}

function Find-EspotaScript {
    Write-Status "Searching for espota.py..."

    if (-not (Test-Path $Arduino15Path)) {
        throw "Arduino15 directory not found: $Arduino15Path"
    }

    $espota = Get-ChildItem $Arduino15Path -Recurse -Filter "espota.py" -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1

    if (-not $espota) {
        throw "espota.py not found. Install ESP32 board package."
    }

    Write-Status "Found: $($espota.FullName)"
    return $espota.FullName
}

function Find-Python {
    $commands = @("python", "py", "python3")

    foreach ($cmd in $commands) {
        $python = Get-Command $cmd -ErrorAction SilentlyContinue
        if ($python) {
            Write-Status "Found Python: $($python.Source)"
            return $python.Source
        }
    }

    throw "Python not found. Install Python and add to PATH."
}

function New-EspotaArgs {
    param(
        [string]$EspotaScript,
        [string]$IP,
        [int]$Port,
        [SecureString]$SecurePassword,
        [string]$FirmwarePath,
        [bool]$Debug
    )

    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecurePassword)
    try {
        $password = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
        $argumentList = @($EspotaScript, '-i', $IP, '-p', $Port, '--auth', $password, '-f', $FirmwarePath)

        if ($Debug) {
            $argumentList = @($EspotaScript, '-d') + $argumentList[1..($argumentList.Count-1)]
        }

        return $argumentList
    }
    finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
    }
}

function Show-Command {
    param([string]$Python, [string[]]$Arguments)

    $masked = @()
    for ($i = 0; $i -lt $Arguments.Count; $i++) {
        if ($Arguments[$i] -eq '--auth') {
            $masked += '--auth', '<PASSWORD>'
            $i++
        }
        elseif ($Arguments[$i] -match '^[A-Za-z]:\\.*\s.*') {
            $masked += "`"$($Arguments[$i])`""
        }
        else {
            $masked += $Arguments[$i]
        }
    }

    Write-Status "Command: $Python $($masked -join ' ')"
}

# --- Main ---
try {
    Write-Status "ESP32 OTA Upload Script" "Success"
    Write-Status "Build Directory: $BuildDir"

    # Check config file
    if (-not (Test-Path $ConfigFile)) {
        throw "Config file not found: $ConfigFile"
    }

    Write-Status "Reading config: $ConfigFile"
    $configContent = Get-Content $ConfigFile -Raw

    # Parse config
    $OTA_IP = Get-ConfigValue $configContent "OTA_IP"
    $OTA_PORT = Get-ConfigValue $configContent "OTA_PORT" -AsNumber

    # Validate config
    if ([string]::IsNullOrWhiteSpace($OTA_IP)) {
        throw "OTA_IP is empty"
    }

    Write-Status "Target: ${OTA_IP}:${OTA_PORT}"

    # Check firmware
    $firmwarePath = Join-Path $BuildDir $FirmwareName
    if (-not (Test-Path $firmwarePath)) {
        throw "Firmware not found: $firmwarePath"
    }

    $firmware = Get-Item $firmwarePath
    $sizeKB = [math]::Round($firmware.Length / 1KB, 1)
    Write-Status "Firmware: $($firmware.Name) ($sizeKB KB)"

    # Find tools
    $espotaScript = Find-EspotaScript
    $python = Find-Python

    # Get password (plain text - ArduinoOTA handles hashing)
    $securePassword = Get-Password $Password

    Write-Status "Password ready for OTA authentication" "Success"

    # Setup debug mode
    $debug = $DebugMode.IsPresent -or ($env:OTA_DEBUG -eq '1')
    if ($debug) {
        Write-Status "Debug mode enabled" "Warning"
    }

    # Build command
    $espotaArgs = New-EspotaArgs $espotaScript $OTA_IP $OTA_PORT $securePassword $firmware.FullName $debug

    Show-Command $python $espotaArgs
    Write-Host ""

    # Execute
    Write-Status "Starting upload..." "Success"
    & $python @espotaArgs

    if ($LASTEXITCODE -eq 0) {
        Write-Status "Upload completed!" "Success"
    }
    else {
        throw "Upload failed (exit code: $LASTEXITCODE)"
    }
}
catch {
    Write-Status "Error: $($_.Exception.Message)" "Error"
    exit 1
}
finally {
    if ($securePassword) {
        $securePassword.Dispose()
    }
}