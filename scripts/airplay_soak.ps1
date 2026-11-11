param(
    [int]$Minutes = 60,
    [int]$IntervalSeconds = 60,
    [string]$OutDir = "build\diagnostics"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path $root $OutDir
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$csvPath = Join-Path $OutDir "airplay-soak-$stamp.csv"
$reportPath = Join-Path $OutDir "airplay-soak-$stamp.txt"
$logPath = Join-Path $env:LOCALAPPDATA "leapbtw\uxplay-windows\logs\uxplay-windows.log"

$proc = Get-Process -Name "uxplay-windows" -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $proc) {
    Write-Error "uxplay-windows is not running"
    exit 1
}

$startPid = $proc.Id
$startTime = Get-Date
$duration = [TimeSpan]::FromMinutes($Minutes)
$cores = [Environment]::ProcessorCount
$samples = New-Object System.Collections.Generic.List[object]
$events = New-Object System.Collections.Generic.List[string]

$prevCpu = $null
$prevTime = $null
$logOffset = 0L
$initialContext = @()
if (Test-Path $logPath) {
    $logOffset = (Get-Item $logPath).Length
    $initialContext = @(Get-Content $logPath -Tail 200 | Where-Object {
            $_ -match 'Client connected|Client disconnected|Begin streaming'
        } | Select-Object -Last 5)
}

function Read-NewLogLines {
    param([string]$Path, [ref]$Offset)

    if (-not (Test-Path $Path)) {
        return @()
    }
    $length = (Get-Item $Path).Length
    if ($length -lt $Offset.Value) {
        # Log rotated or truncated: restart from the beginning.
        $Offset.Value = 0
    }
    if ($length -eq $Offset.Value) {
        return @()
    }

    $stream = [System.IO.File]::Open(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite)
    try {
        [void]$stream.Seek($Offset.Value, [System.IO.SeekOrigin]::Begin)
        $count = [int]($length - $Offset.Value)
        $buffer = New-Object byte[] $count
        $read = $stream.Read($buffer, 0, $count)
        $Offset.Value += $read
        $text = [System.Text.Encoding]::UTF8.GetString($buffer, 0, $read)
        return ($text -split "`r?`n")
    } finally {
        $stream.Dispose()
    }
}

Write-Output ("AirPlay soak: pid={0} minutes={1} interval={2}s" -f
    $startPid, $Minutes, $IntervalSeconds)
Write-Output ("csv: {0}" -f $csvPath)

while ($true) {
    $now = Get-Date
    if (($now - $startTime) -ge $duration) {
        break
    }

    $newLines = @(Read-NewLogLines -Path $logPath -Offset ([ref]$logOffset))
    $errorLines = @($newLines | Where-Object {
        $_ -match '\[ERROR\]|\[WARN\]|freeze|not responding'
    })
    $keyLines = @($newLines | Where-Object {
        $_ -match 'Client connected|Client disconnected|Begin streaming|raop_rtp_mirror|\[ERROR\]|\[WARN\]'
    })
    foreach ($line in $keyLines) {
        if ($line.Trim().Length -gt 0) {
            $events.Add($line.Trim())
        }
    }

    $p = Get-Process -Id $startPid -ErrorAction SilentlyContinue
    if (-not $p -or $p.ProcessName -ne "uxplay-windows") {
        $samples.Add([pscustomobject]@{
                Time = $now.ToString("s")
                ElapsedSec = [int]($now - $startTime).TotalSeconds
                Pid = $startPid
                CpuPctAllCores = $null
                CpuPctOneCore = $null
                CpuTotalSec = $null
                WorkingSetMB = $null
                PrivateMB = $null
                Threads = $null
                Handles = $null
                MainWindowTitle = "PROCESS_EXITED"
                NewErrors = $errorLines.Count
                LogBytes = $logOffset
            })
        Write-Output ("[sample] process exited at {0}" -f $now.ToString("HH:mm:ss"))
        break
    }

    $cpu = $p.TotalProcessorTime.TotalSeconds
    $cpuAll = $null
    $cpuOne = $null
    if ($null -ne $prevCpu) {
        $dt = ($now - $prevTime).TotalSeconds
        if ($dt -gt 0) {
            $cpuOne = [math]::Round((($cpu - $prevCpu) / $dt) * 100, 1)
            $cpuAll = [math]::Round($cpuOne / $cores, 1)
        }
    }

    $workingSet = [math]::Round($p.WorkingSet64 / 1MB, 1)
    $privateBytes = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
    $title = ""
    try {
        $title = $p.MainWindowTitle
    } catch {
        $title = ""
    }

    $sample = [pscustomobject]@{
        Time = $now.ToString("s")
        ElapsedSec = [int]($now - $startTime).TotalSeconds
        Pid = $p.Id
        CpuPctAllCores = $cpuAll
        CpuPctOneCore = $cpuOne
        CpuTotalSec = [math]::Round($cpu, 1)
        WorkingSetMB = $workingSet
        PrivateMB = $privateBytes
        Threads = $p.Threads.Count
        Handles = $p.HandleCount
        MainWindowTitle = $title
        NewErrors = $errorLines.Count
        LogBytes = $logOffset
    }
    $samples.Add($sample)
    Write-Output ("[sample] {0} elapsed={1}s rss={2}MB cpuAll={3}% cpu1={4}% err={5}" -f
        $now.ToString("HH:mm:ss"), $sample.ElapsedSec, $workingSet,
        $cpuAll, $cpuOne, $errorLines.Count)

    $prevCpu = $cpu
    $prevTime = $now

    $remaining = ($startTime + $duration) - (Get-Date)
    if ($remaining.TotalSeconds -le 0) {
        break
    }
    $sleepSeconds = [math]::Min(
        $IntervalSeconds,
        [math]::Max(1, [int]$remaining.TotalSeconds))
    Start-Sleep -Seconds $sleepSeconds
}

$samples | Export-Csv -Path $csvPath -NoTypeInformation -Encoding UTF8

$endTime = Get-Date
$rssValues = @($samples | Where-Object { $null -ne $_.WorkingSetMB } |
        ForEach-Object { $_.WorkingSetMB })
$cpuValues = @($samples | Where-Object { $null -ne $_.CpuPctAllCores } |
        ForEach-Object { $_.CpuPctAllCores })
$errorTotal = ($samples | Measure-Object -Property NewErrors -Sum).Sum
if ($null -eq $errorTotal) {
    $errorTotal = 0
}

$report = New-Object System.Collections.Generic.List[string]
$report.Add("AirPlay soak report")
$report.Add(("start:   {0}" -f $startTime.ToString("yyyy-MM-dd HH:mm:ss")))
$report.Add(("end:     {0}" -f $endTime.ToString("yyyy-MM-dd HH:mm:ss")))
$report.Add(("pid:     {0}" -f $startPid))
$report.Add(("samples: {0}  interval: {1}s" -f $samples.Count, $IntervalSeconds))
$report.Add(("errors/warnings: {0}" -f $errorTotal))
if ($rssValues.Count -gt 0) {
    $report.Add(("rss MB: first={0} last={1} min={2} max={3}" -f
            $rssValues[0], $rssValues[-1],
            ($rssValues | Measure-Object -Minimum).Minimum,
            ($rssValues | Measure-Object -Maximum).Maximum))
}
if ($cpuValues.Count -gt 0) {
    $report.Add(("cpu all-cores %: avg={0} max={1}" -f
            [math]::Round(($cpuValues | Measure-Object -Average).Average, 1),
            ($cpuValues | Measure-Object -Maximum).Maximum))
}
$report.Add("")
$report.Add("initial context:")
if ($initialContext.Count -eq 0) {
    $report.Add("  (none)")
} else {
    foreach ($line in $initialContext) {
        $report.Add(("  {0}" -f $line.Trim()))
    }
}
$report.Add("")
$report.Add("log events:")
if ($events.Count -eq 0) {
    $report.Add("  (none)")
} else {
    foreach ($eventLine in ($events | Select-Object -Last 40)) {
        $report.Add(("  {0}" -f $eventLine))
    }
}

$report | Set-Content -Path $reportPath -Encoding UTF8
Write-Output ("report: {0}" -f $reportPath)
foreach ($line in $report) {
    Write-Output $line
}
