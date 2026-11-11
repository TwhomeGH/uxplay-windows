#Requires -RunAsAdministrator

param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$managedGroup = "uxplay-windows"

function Get-StaleUxPlayRules {
    Get-NetFirewallRule -ErrorAction SilentlyContinue | Where-Object {
        if ($_.Group -eq $managedGroup) {
            return $false
        }
        if ($_.DisplayName -match 'uxplay') {
            return $true
        }
        $apps = @(Get-NetFirewallApplicationFilter -AssociatedNetFirewallRule $_ -ErrorAction SilentlyContinue)
        return @($apps | Where-Object { $_.Program -match 'uxplay' }).Count -gt 0
    }
}

$rules = @(Get-StaleUxPlayRules)
if ($rules.Count -eq 0) {
    Write-Host "No stale uxplay firewall rules found."
    return
}

Write-Host "Stale uxplay firewall rules:"
$rules |
    Select-Object DisplayName, Name, Group, Direction, Action, Enabled |
    Format-Table -AutoSize

if (-not $Force) {
    $answer = Read-Host "Remove these $($rules.Count) rule(s)? [y/N]"
    if ($answer -notmatch '^[yY]') {
        Write-Host "Cancelled."
        return
    }
}

$removed = 0
foreach ($rule in $rules) {
    try {
        Remove-NetFirewallRule -Name $rule.Name -ErrorAction Stop
        Write-Host "Removed: $($rule.DisplayName)"
        $removed++
    } catch {
        Write-Warning "Could not remove $($rule.DisplayName): $_"
    }
}

Write-Host "Removed $removed of $($rules.Count) rule(s)."
Write-Host "The installer-managed group '$managedGroup' was kept."
