<#
.SYNOPSIS
    Prepares the EasyEffects Virtual Audio driver for Microsoft attestation signing,
    so it loads with Secure Boot ON (no test mode).

.DESCRIPTION
    Windows with Secure Boot only loads kernel drivers signed by Microsoft. For a
    driver like this one, that means "attestation signing" through the Windows
    Hardware Dev Center (Partner Center):

      1. One-time: get an EV code-signing certificate (from DigiCert, Sectigo, GlobalSign, ...)
         and register for the Windows Hardware Developer Program with it.
      2. Run this script. It builds the driver, packs .inf/.sys/.pdb into a CAB and
         signs the CAB with your EV certificate.
      3. Upload driver\attestation\EEVirtualAudio.cab in Partner Center:
         Hardware > Submit new hardware > select "Windows 10/11 x64" attestation.
      4. Download the signed package Microsoft returns and extract it to driver\signed\.
      5. Run:  scripts\build_driver_installer.ps1 -Release
         The resulting EEVirtualAudioSetup.exe installs on normal Secure Boot PCs.

.PARAMETER CertThumbprint
    SHA-1 thumbprint of your EV code-signing certificate (Cert:\CurrentUser\My or on the token).

.PARAMETER TimestampUrl
    RFC 3161 timestamp server.
#>
param(
    [Parameter(Mandatory = $true)][string]$CertThumbprint,
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"
$Root = (Get-Item "$PSScriptRoot\..").FullName
Set-Location $Root

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
$msbuild = Join-Path $vs "MSBuild\Current\Bin\amd64\MSBuild.exe"
$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter signtool.exe |
    Where-Object { $_.FullName -match "\\x64\\" } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $signtool) { Write-Error "signtool.exe not found (install the Windows SDK)."; exit 1 }

# 1. Build
Write-Host "Building driver..."
& $msbuild driver\EEVirtualAudio.vcxproj /p:Configuration=Release /p:Platform=x64 /nodeReuse:false /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$out = Join-Path $Root "driver\build\x64\Release"
$pkg = Join-Path $out "EEVirtualAudio"

# 2. Stage + CAB
$attDir = Join-Path $Root "driver\attestation"
New-Item -ItemType Directory -Force -Path $attDir | Out-Null
$cab = Join-Path $attDir "EEVirtualAudio.cab"
if (Test-Path $cab) { Remove-Item $cab -Force -Confirm:$false }

$files = @((Join-Path $pkg "EEVirtualAudio.inf"), (Join-Path $pkg "EEVirtualAudio.sys"))
$pdb = Join-Path $out "EEVirtualAudio.pdb"
if (Test-Path $pdb) { $files += $pdb }

$ddf = Join-Path $attDir "EEVirtualAudio.ddf"
$lines = @(
    ".OPTION EXPLICIT",
    ".Set CabinetFileCountThreshold=0",
    ".Set FolderFileCountThreshold=0",
    ".Set FolderSizeThreshold=0",
    ".Set MaxCabinetSize=0",
    ".Set MaxDiskFileCount=0",
    ".Set MaxDiskSize=0",
    ".Set CompressionType=MSZIP",
    ".Set Cabinet=on",
    ".Set Compress=on",
    ".Set CabinetNameTemplate=EEVirtualAudio.cab",
    ".Set DiskDirectoryTemplate=`"$attDir`"",
    ".Set DestinationDir=EEVirtualAudio"
) + ($files | ForEach-Object { "`"$_`"" })
Set-Content -Path $ddf -Value $lines -Encoding ASCII

Write-Host "Creating CAB..."
& makecab.exe /F $ddf | Out-Null
if (-not (Test-Path $cab)) { Write-Error "makecab failed."; exit 1 }
Remove-Item (Join-Path $Root "setup.inf"), (Join-Path $Root "setup.rpt") -ErrorAction SilentlyContinue -Confirm:$false

# 3. EV-sign the CAB
Write-Host "Signing CAB with certificate $CertThumbprint..."
& $signtool.FullName sign /fd sha256 /sha1 $CertThumbprint /tr $TimestampUrl /td sha256 $cab
if ($LASTEXITCODE -ne 0) { Write-Error "signtool failed."; exit $LASTEXITCODE }

Write-Host ""
Write-Host "Ready for submission: $cab"
Write-Host "Next: upload it in Partner Center (Hardware > Submit new hardware, attestation, Windows 10/11 x64),"
Write-Host "      extract the signed result to driver\signed\, then run scripts\build_driver_installer.ps1 -Release"
