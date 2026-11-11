@echo off
rem Runs package_release.ps1 without needing to change the PowerShell execution policy.
rem Usage: package_release.cmd [-Version v0.2.0] [-NoZip]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_release.ps1" %*
set RC=%ERRORLEVEL%
if "%~1"=="" pause
exit /b %RC%
