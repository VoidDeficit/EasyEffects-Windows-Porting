<#
.SYNOPSIS
    Builds EEVirtualAudioSetup.exe (the virtual audio driver installer).

.DESCRIPTION
    Development (default):
        Builds driver\EEVirtualAudio.vcxproj (test-signed) and embeds it plus the
        WDK test certificate. Installs only with test signing on / Secure Boot off.

    Release (-Release):
        Embeds the Microsoft attestation-signed package from -SignedPackage
        (see scripts\sign_driver_attestation.ps1 and driver\README.md).
        Installs and loads with Secure Boot ON.

    Output: build-installer\Release\EEVirtualAudioSetup.exe
#>
param(
    [switch]$Release,
    [string]$SignedPackage = "driver\signed"
)

$ErrorActionPreference = "Stop"
$Root = (Get-Item "$PSScriptRoot\..").FullName
Set-Location $Root

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
$msbuild = Join-Path $vs "MSBuild\Current\Bin\amd64\MSBuild.exe"
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }

if ($Release) {
    $inf = Get-ChildItem (Join-Path $Root $SignedPackage) -Recurse -Filter "EEVirtualAudio.inf" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $inf) {
        Write-Error "No signed package in $SignedPackage. Extract the package Microsoft returned from attestation signing there."
        exit 1
    }
    $packageDir = $inf.DirectoryName
    $cert = ""
    Write-Host "Release installer from signed package: $packageDir"
} else {
    Write-Host "Building driver (test-signed)..."
    & $msbuild driver\EEVirtualAudio.vcxproj /p:Configuration=Release /p:Platform=x64 /nodeReuse:false /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $packageDir = Join-Path $Root "driver\build\x64\Release\EEVirtualAudio"
    $cert = Join-Path $Root "driver\build\x64\Release\EEVirtualAudio.cer"
}

& $cmake -S installer -B build-installer -G "Visual Studio 17 2022" -A x64 "-DEEVAD_PACKAGE_DIR=$packageDir" "-DEEVAD_TEST_CERT=$cert"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build build-installer --config Release -- /nodeReuse:false /v:minimal
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $Root "build-installer\Release\EEVirtualAudioSetup.exe"
Write-Host ""
Write-Host "Built: $exe"
if (-not $Release) {
    Write-Host "NOTE: test-signed. Installs only with test signing on and Secure Boot off."
}
