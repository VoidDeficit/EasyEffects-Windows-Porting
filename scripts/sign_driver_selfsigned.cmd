@echo off
rem Runs sign_driver_selfsigned.ps1 without needing to change the PowerShell execution policy.
rem Usage: sign_driver_selfsigned.cmd [-Subject "Name"] [-SkipBuild]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sign_driver_selfsigned.ps1" %*
set RC=%ERRORLEVEL%
if "%~1"=="" pause
exit /b %RC%
