<#
.SYNOPSIS
  Run one windows_builder step detached via a demand-only scheduled task.

.DESCRIPTION
  SSH drops kill child processes, so long steps (gclient sync, compiles) run
  as a scheduled task with Normal priority (4; the default 7 trips Chromium's
  CheckProcessPriority) and zero triggers (never auto-fires). Output goes to
  C:\src\maho-win-builder\<Step>.out and a <Step>.done/.failed sentinel.

.EXAMPLE
  powershell -File run-detached.ps1 -Step 10-checkout
#>
param([Parameter(Mandatory)][string]$Step, [string]$StepArgs = '')
$ErrorActionPreference = 'Stop'
$State = 'C:\src\maho-win-builder'; New-Item -ItemType Directory -Force -Path $State | Out-Null
$script = Join-Path $PSScriptRoot "$Step.ps1"
if (-not (Test-Path $script)) { throw "no such step: $script" }
Remove-Item (Join-Path $State "$Step.done"), (Join-Path $State "$Step.failed") -EA SilentlyContinue
$wrapper = Join-Path $State "$Step.cmd"
Set-Content -Path $wrapper -Encoding ascii -Value @"
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "$script" $StepArgs > "$State\$Step.out" 2>&1
if errorlevel 1 (echo %ERRORLEVEL% > "$State\$Step.failed") else (echo ok > "$State\$Step.done")
"@
$name = "MahoWinBuilder-$Step"
if (Get-ScheduledTask -TaskName $name -EA SilentlyContinue) { Unregister-ScheduledTask -TaskName $name -Confirm:$false }
$a = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument "/c `"$wrapper`""
$s = New-ScheduledTaskSettingsSet -Priority 4 -ExecutionTimeLimit (New-TimeSpan -Hours 12) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
$p = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType S4U -RunLevel Highest
Register-ScheduledTask -TaskName $name -Action $a -Settings $s -Principal $p | Out-Null
$t = Get-ScheduledTask -TaskName $name
if ($t.Triggers.Count -ne 0 -or $t.Settings.Priority -ne 4) { throw 'task registration invariant violated' }
Start-ScheduledTask -TaskName $name
"STARTED $name"
