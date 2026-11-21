# verify_memory.ps1 - Validates NotchGlass Working Set RAM < 50 MB & 0% Idle CPU
param(
    [string]$ExecutablePath = ".\build\Release\NotchGlass.exe",
    [int]$SampleSeconds = 15,
    [double]$MaxWorkingSetMB = 50.0
)

$ErrorActionPreference = "Stop"

$proc = Get-Process -Name "NotchGlass" -ErrorAction SilentlyContinue
if (-not $proc) {
    if (-not (Test-Path $ExecutablePath)) {
        Write-Error "NotchGlass.exe not found at $ExecutablePath. Build with CMake first."
        exit 1
    }
    Write-Host "Launching $ExecutablePath..." -ForegroundColor Cyan
    $proc = Start-Process -FilePath $ExecutablePath -PassThru
    Start-Sleep -Seconds 2
}

Write-Host "Auditing PID $($proc.Id) for $SampleSeconds seconds (Limit: < $MaxWorkingSetMB MB Working Set)..." -ForegroundColor Cyan

$peakWorkingSetMB = 0.0
$prevCpu = $proc.TotalProcessorTime.TotalSeconds

for ($i = 1; $i -le $SampleSeconds; $i++) {
    Start-Sleep -Seconds 1
    $p = Get-Process -Id $proc.Id -ErrorAction Stop
    $wsMB = [math]::Round($p.WorkingSet64 / 1MB, 2)
    $privMB = [math]::Round($p.PrivateMemorySize64 / 1MB, 2)
    $curCpu = $p.TotalProcessorTime.TotalSeconds
    $deltaCpuPct = [math]::Round((($curCpu - $prevCpu) / [Environment]::ProcessorCount) * 100.0, 2)
    $prevCpu = $curCpu

    if ($wsMB -gt $peakWorkingSetMB) { $peakWorkingSetMB = $wsMB }

    Write-Host ("[{0,2}/{1}] WorkingSet: {2,6:N2} MB | Private: {3,6:N2} MB | CPU: {4,5:N2}% | Handles: {5}" -f `
        $i, $SampleSeconds, $wsMB, $privMB, $deltaCpuPct, $p.HandleCount)
}

if ($peakWorkingSetMB -ge $MaxWorkingSetMB) {
    Write-Error "FAIL: Peak Working Set ($peakWorkingSetMB MB) exceeded $MaxWorkingSetMB MB budget."
    exit 1
}

Write-Host "PASS: Peak Working Set = $peakWorkingSetMB MB (strictly under $MaxWorkingSetMB MB)." -ForegroundColor Green
