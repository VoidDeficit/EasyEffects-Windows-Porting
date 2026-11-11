<#
.SYNOPSIS
    Removes the EasyEffects Virtual Audio device and its driver package.
    Run from an elevated PowerShell.
#>
$ErrorActionPreference = "Stop"
$HardwareId = "ROOT\EasyEffectsVirtualAudio"

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "Run this script from an elevated (Administrator) PowerShell."
    exit 1
}

$devices = Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.HardwareID -contains $HardwareId }
foreach ($d in $devices) {
    Write-Host "Removing device $($d.InstanceId)"
    & pnputil /remove-device $d.InstanceId | Out-Host
}

# Remove every oem*.inf that is ours.
$drivers = Get-WindowsDriver -Online -ErrorAction SilentlyContinue |
    Where-Object { $_.OriginalFileName -like "*EEVirtualAudio.inf" }
foreach ($drv in $drivers) {
    Write-Host "Deleting driver package $($drv.Driver)"
    & pnputil /delete-driver $drv.Driver /uninstall /force | Out-Host
}

Write-Host "EasyEffects Virtual Audio removed."
