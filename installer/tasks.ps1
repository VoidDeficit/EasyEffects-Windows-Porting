# Registers or removes the Task Scheduler tasks that let EasyEffects run with
# administrator rights without a UAC prompt each time (Synchronous Audio Router
# only creates its Windows devices for an elevated host).
#
#   \EasyEffects\Launch     on demand; the app starts it when opened normally
#   \EasyEffects\Autostart  at sign-in, starts hidden in the tray (optional)
#
# Launch allows parallel runs: a second EasyEffects started by it hands over
# to the one already running (single instance), which shows its window.
#
# Run elevated (the installer does). Usage:
#   tasks.ps1 -Install -Exe "C:\Program Files\EasyEffects\EasyEffects.exe" [-Autostart]
#   tasks.ps1 -SetAutostart on|off -Exe "...\EasyEffects.exe"
#   tasks.ps1 -Uninstall
param(
    [switch]$Install,
    [switch]$Uninstall,
    [switch]$Autostart,
    [ValidateSet("", "on", "off")][string]$SetAutostart = "",
    [string]$Exe
)

$ErrorActionPreference = "Stop"
$folder = "\EasyEffects\"
$user = if ($env:USERDOMAIN) { "$env:USERDOMAIN\$env:USERNAME" } else { $env:USERNAME }

function Remove-Task([string]$name) {
    if (Get-ScheduledTask -TaskPath $folder -TaskName $name -ErrorAction SilentlyContinue) {
        Unregister-ScheduledTask -TaskPath $folder -TaskName $name -Confirm:$false
    }
}

function New-Task([string]$name, $trigger, [string]$argument = "") {
    $actionArgs = @{ Execute = $Exe; WorkingDirectory = (Split-Path $Exe) }
    if ($argument) { $actionArgs.Argument = $argument }
    $action    = New-ScheduledTaskAction @actionArgs
    $principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
    $settings  = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
                    -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances Parallel -Priority 4
    $taskArgs = @{ TaskPath = $folder; TaskName = $name; Action = $action; Principal = $principal; Settings = $settings; Force = $true }
    if ($trigger) { $taskArgs.Trigger = $trigger }
    Register-ScheduledTask @taskArgs | Out-Null
}

if ($Uninstall) {
    Remove-Task "Launch"
    Remove-Task "Autostart"
    exit 0
}

if (-not $Exe -or -not (Test-Path $Exe)) { throw "EasyEffects.exe not found: '$Exe'" }

if ($Install) {
    New-Task "Launch" $null
    if ($Autostart) { New-Task "Autostart" (New-ScheduledTaskTrigger -AtLogOn -User $user) "--tray" }
    else { Remove-Task "Autostart" }
    exit 0
}

if ($SetAutostart -eq "on")  { New-Task "Autostart" (New-ScheduledTaskTrigger -AtLogOn -User $user) "--tray"; exit 0 }
if ($SetAutostart -eq "off") { Remove-Task "Autostart"; exit 0 }

throw "Nothing to do: pass -Install, -Uninstall or -SetAutostart."
