<#
.SYNOPSIS
    Installs the EasyEffects Virtual Audio driver (root-enumerated device).

.DESCRIPTION
    Must be run from an elevated PowerShell. Expects a built driver package
    (EEVirtualAudio.inf/.sys/.cat), either next to this script (release zip)
    or under driver\build\<Platform>\<Configuration>\ (source build).

    Test-signed builds need Windows test signing mode:
        bcdedit /set testsigning on   (then reboot; Secure Boot must be off)
    This script does NOT change that setting for you.

.PARAMETER PackageDir
    Folder containing EEVirtualAudio.inf, .sys and .cat. Auto-detected if omitted.

.PARAMETER DevCon
    Path to devcon.exe. Auto-detected from the WDK / script folder if omitted.
#>
param(
    [string]$PackageDir,
    [string]$DevCon,
    [ValidateSet("x64", "ARM64")][string]$Platform = "x64",
    [ValidateSet("Release", "Debug")][string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$HardwareId = "ROOT\EasyEffectsVirtualAudio"

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "Run this script from an elevated (Administrator) PowerShell."
    exit 1
}

# --- Locate the driver package ----------------------------------------------
if (-not $PackageDir) {
    $candidates = @(
        $PSScriptRoot,
        (Join-Path $PSScriptRoot "build\$Platform\$Configuration\EEVirtualAudio"),
        (Join-Path $PSScriptRoot "build\$Platform\$Configuration")
    )
    foreach ($c in $candidates) {
        if ((Test-Path (Join-Path $c "EEVirtualAudio.inf")) -and (Test-Path (Join-Path $c "EEVirtualAudio.sys"))) {
            $PackageDir = $c
            break
        }
    }
}
if (-not $PackageDir -or -not (Test-Path (Join-Path $PackageDir "EEVirtualAudio.inf"))) {
    Write-Error "Driver package not found. Build driver\EEVirtualAudio.vcxproj first or pass -PackageDir."
    exit 1
}
$Inf = Join-Path $PackageDir "EEVirtualAudio.inf"
Write-Host "Driver package: $PackageDir"

# --- Test-signing certificate ---------------------------------------------
$cer = Get-ChildItem -Path $PackageDir, (Split-Path $PackageDir -Parent) -Filter "*.cer" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($cer) {
    Write-Host "Trusting test certificate $($cer.Name) (LocalMachine Root + TrustedPublisher)"
    Import-Certificate -FilePath $cer.FullName -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $cer.FullName -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null

    $bcd = (& bcdedit /enum "{current}") -join "`n"
    if ($bcd -notmatch "testsigning\s+Yes") {
        Write-Warning "Test signing is OFF. A test-signed driver will not load until you run 'bcdedit /set testsigning on' and reboot."
    }
}

# --- Locate devcon ---------------------------------------------------------
if (-not $DevCon) {
    $devconCandidates = @(Join-Path $PSScriptRoot "devcon.exe")
    $kits = "${env:ProgramFiles(x86)}\Windows Kits\10\Tools"
    if (Test-Path $kits) {
        $devconCandidates += Get-ChildItem $kits -Recurse -Filter devcon.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\$Platform\\" } |
            Sort-Object FullName -Descending | ForEach-Object { $_.FullName }
    }
    $DevCon = $devconCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
}
if (-not $DevCon) {
    Write-Error "devcon.exe not found. Install the WDK or place devcon.exe next to this script."
    exit 1
}

# --- Install or update ----------------------------------------------------
$existing = Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.HardwareID -contains $HardwareId }
if ($existing) {
    Write-Host "Device already present, updating driver..."
    & $DevCon update $Inf $HardwareId
} else {
    Write-Host "Creating root device $HardwareId..."
    & $DevCon install $Inf $HardwareId
}

if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 1) {
    Write-Error "devcon failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}
if ($LASTEXITCODE -eq 1) {
    Write-Warning "A reboot is required to finish the installation."
}

Write-Host ""
Write-Host "Done. New audio endpoints:"
Write-Host "  Playback:  EasyEffects Speakers, EasyEffects Mic Feed"
Write-Host "  Recording: EasyEffects Speakers Monitor, EasyEffects Microphone"
Write-Host "Open EasyEffects and pick a mode from the 'Virtual Audio' button."
