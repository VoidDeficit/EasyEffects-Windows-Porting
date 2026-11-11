<#
.SYNOPSIS
    Builds the EasyEffects Virtual Audio driver, signs it with YOUR OWN self-signed
    code-signing certificate, and produces a self-signed EEVirtualAudioSetup.exe.

.DESCRIPTION
    This is the "self-signed" path (as used by SSDE / CustomKernelSigners). On its
    own it does NOT make the driver loadable: a self-signed kernel driver still
    needs Secure Boot off + test signing, OR Windows' CustomKernelSigners (CKS)
    feature enabled on a machine whose Secure Boot Platform Key you own.

    This script does only the parts that are safe and reversible:
      * create (once) a self-signed code-signing certificate in your user store,
      * embed-sign EEVirtualAudio.sys and its .cat with it,
      * export the public certificate (.cer),
      * build EEVirtualAudioSetup.exe embedding that signed package + certificate.

    It does NOT, and this script will never: change Secure Boot keys, enable CKS,
    change code-integrity policy, or run bcdedit. Those are up to you (see
    driver\README.md -> "Loading a self-signed driver with Secure Boot on").

.PARAMETER Subject
    Certificate subject (CN). Reused if a cert with this subject already exists.

.PARAMETER SkipBuild
    Don't rebuild the driver; sign whatever is already in driver\build\x64\Release.
#>
param(
    [string]$Subject = "EasyEffects Virtual Audio (Self-Signed)",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = (Get-Item "$PSScriptRoot\..").FullName
Set-Location $Root

function Find-Tool($name, $arch = "x64") {
    Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter $name -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "\\$arch\\" } | Sort-Object FullName -Descending | Select-Object -First 1
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
$msbuild = Join-Path $vs "MSBuild\Current\Bin\amd64\MSBuild.exe"
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }
$signtool = Find-Tool "signtool.exe"
if (-not $signtool) { Write-Error "signtool.exe not found (install the Windows SDK)."; exit 1 }

# 1. Build the driver (test-signed by the WDK; we re-sign it below).
if (-not $SkipBuild) {
    Write-Host "Building driver..."
    & $msbuild driver\EEVirtualAudio.vcxproj /p:Configuration=Release /p:Platform=x64 /nodeReuse:false /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$out = Join-Path $Root "driver\build\x64\Release"
$pkg = Join-Path $out "EEVirtualAudio"
$sys = Join-Path $pkg "EEVirtualAudio.sys"
$cat = Join-Path $pkg "EEVirtualAudio.cat"
if (-not (Test-Path $sys)) { Write-Error "Driver not built: $sys missing. Build it first or drop -SkipBuild."; exit 1 }

# 2. Get or create the self-signed code-signing certificate.
$cn = "CN=$Subject"
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $cn -and $_.HasPrivateKey } |
    Sort-Object NotAfter -Descending | Select-Object -First 1
if ($cert) {
    Write-Host "Using existing certificate $($cert.Thumbprint)"
} else {
    Write-Host "Creating self-signed code-signing certificate '$Subject'..."
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $cn `
        -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy Exportable `
        -KeyUsage DigitalSignature -HashAlgorithm SHA256 `
        -NotAfter (Get-Date).AddYears(10) `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")  # EKU: Code Signing
    Write-Host "Created $($cert.Thumbprint)"
}

# 3. Export the public certificate next to the package (installer embeds it to trust).
$cer = Join-Path $out "EEVirtualAudio.cer"
Export-Certificate -Cert $cert -FilePath $cer -Type CERT -Force | Out-Null

# 4. Embed-sign the .sys and the .cat with it (replaces the WDK test signature).
$ts = "http://timestamp.digicert.com"
foreach ($file in @($sys, $cat)) {
    Write-Host "Signing $(Split-Path $file -Leaf)..."
    & $signtool.FullName sign /fd sha256 /sha1 $cert.Thumbprint /tr $ts /td sha256 $file
    if ($LASTEXITCODE -ne 0) { Write-Error "signtool failed on $file"; exit $LASTEXITCODE }
}

# 5. Build the installer embedding the self-signed package + certificate.
Write-Host "Building self-signed installer..."
& $cmake -S installer -B build-installer -G "Visual Studio 17 2022" -A x64 "-DEEVAD_PACKAGE_DIR=$pkg" "-DEEVAD_TEST_CERT=$cer"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build build-installer --config Release -- /nodeReuse:false /v:minimal
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $Root "build-installer\Release\EEVirtualAudioSetup.exe"
Write-Host ""
Write-Host "Built: $exe"
Write-Host "Signed with: $($cert.Subject)  [$($cert.Thumbprint)]"
Write-Host ""
Write-Host "This driver is SELF-signed. To actually load it with Secure Boot ON you must"
Write-Host "own your machine's Secure Boot Platform Key and enable Windows CustomKernelSigners"
Write-Host "(e.g. via SSDE). See driver\README.md -> 'Loading a self-signed driver with Secure Boot on'."
