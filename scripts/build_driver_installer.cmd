@echo off
rem Runs build_driver_installer.ps1 without needing to change the PowerShell execution policy.
rem Usage: build_driver_installer.cmd [-Release] [-SignedPackage <folder>]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_driver_installer.ps1" %*
set RC=%ERRORLEVEL%
if "%~1"=="" pause
exit /b %RC%
