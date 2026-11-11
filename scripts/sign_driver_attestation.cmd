@echo off
rem Runs sign_driver_attestation.ps1 without needing to change the PowerShell execution policy.
rem Usage: sign_driver_attestation.cmd -CertThumbprint <EV cert SHA1 thumbprint>
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sign_driver_attestation.ps1" %*
exit /b %ERRORLEVEL%
