# install_windows.ps1 - Clean Native C++20 Build, Icon Install & Launch
$ErrorActionPreference = "Continue"

$RootDir = Split-Path -Parent $PSScriptRoot
$LogPath = Join-Path $RootDir "install_log.txt"
"=== NotchGlass Install Log ($(Get-Date -Format s)) ===" | Out-File -FilePath $LogPath -Encoding utf8

function Find-VSInfo {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        return $null
    }
    $rawPath = @(& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
    $vsPath = $rawPath | Where-Object { $_ -and $_.Trim().Length -gt 0 } | Select-Object -First 1
    if (-not $vsPath) {
        return $null
    }
    $rawVer = @(& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion 2>$null)
    $vsVer = $rawVer | Where-Object { $_ -and $_.Trim().Length -gt 0 } | Select-Object -First 1
    $generator = "Visual Studio 17 2022"
    if ($vsVer -and $vsVer.Trim().StartsWith("18.")) {
        $generator = "Visual Studio 18 2026"
    } elseif ($vsVer -and $vsVer.Trim().StartsWith("16.")) {
        $generator = "Visual Studio 16 2019"
    }
    return @{
        Path = $vsPath.Trim()
        Generator = $generator
    }
}

function Find-CMake([string]$VsPath) {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source -and (Test-Path $cmd.Source)) {
        return $cmd.Source
    }
    $candidates = @(
        "$env:ProgramFiles\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe",
        "$env:LOCALAPPDATA\Programs\CMake\bin\cmake.exe"
    )
    if ($VsPath) {
        $candidates += (Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
    }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) {
            return $c
        }
    }
    return $null
}

function Invoke-CMakeStep([string]$Exe, [string[]]$ArgsList, [string]$LogFile) {
    & $Exe @ArgsList 2>&1 | ForEach-Object {
        $line = $_.ToString()
        Write-Host $line
        Add-Content -Path $LogFile -Value $line -Encoding UTF8
    }
    return $LASTEXITCODE
}

Write-Host "[1/5] Checking Visual Studio C++20 Build Tools & CMake..." -ForegroundColor Cyan
$vsInfo = Find-VSInfo
if (-not $vsInfo) {
    Write-Host "Installing Microsoft C++ Build Tools (this may take a few minutes)..." -ForegroundColor Yellow
    $vsSetup = Join-Path $env:TEMP "vs_BuildTools.exe"
    Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vs_BuildTools.exe" -OutFile $vsSetup -UseBasicParsing
    $vp = Start-Process -FilePath $vsSetup -ArgumentList "--passive --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" -Wait -PassThru
    $vsInfo = Find-VSInfo
    if (-not $vsInfo) {
        Write-Host "[ERROR] Visual Studio C++ Build Tools were not detected (exit $($vp.ExitCode))." -ForegroundColor Red
        exit 1
    }
}
Write-Host ("  Found VS: " + $vsInfo.Path + " (" + $vsInfo.Generator + ")") -ForegroundColor Green

$cmakeExe = Find-CMake $vsInfo.Path
if (-not $cmakeExe) {
    Write-Host "Installing CMake via winget..." -ForegroundColor Yellow
    $cp = Start-Process -FilePath "winget" -ArgumentList "install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements" -Wait -PassThru
    $cmakeExe = Find-CMake $vsInfo.Path
    if (-not $cmakeExe) {
        Write-Host "[ERROR] CMake not found after installation (exit $($cp.ExitCode))." -ForegroundColor Red
        exit 1
    }
}
Write-Host ("  Found CMake: " + $cmakeExe) -ForegroundColor Green

Write-Host "[2/5] Preparing clean build directory..." -ForegroundColor Cyan
Get-Process -Name "NotchGlass" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

$buildDir = Join-Path $RootDir "build"
if (Test-Path $buildDir) {
    Remove-Item -Recurse -Force $buildDir -ErrorAction SilentlyContinue
}

Write-Host "[3/5] Configuring and compiling NotchGlass.exe (Release x64)..." -ForegroundColor Cyan
$cfgExit = Invoke-CMakeStep $cmakeExe @("-S", $RootDir, "-B", $buildDir, "-G", $vsInfo.Generator, "-A", "x64") $LogPath
if ($cfgExit -ne 0) {
    Write-Host "[ERROR] CMake configure failed (exit $cfgExit). See install_log.txt." -ForegroundColor Red
    exit 1
}

$bldExit = Invoke-CMakeStep $cmakeExe @("--build", $buildDir, "--config", "Release") $LogPath
if ($bldExit -ne 0) {
    Write-Host "[ERROR] C++ compilation failed (exit $bldExit). See install_log.txt." -ForegroundColor Red
    exit 1
}

$builtExe = Join-Path $buildDir "Release\NotchGlass.exe"
if (-not (Test-Path $builtExe)) {
    Write-Host "[ERROR] Build completed but NotchGlass.exe was not found at $builtExe" -ForegroundColor Red
    exit 1
}

Write-Host "[4/5] Installing NotchGlass app and creating shortcuts..." -ForegroundColor Cyan
$installDir = Join-Path $env:LOCALAPPDATA "NotchGlass"
if (-not (Test-Path $installDir)) {
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
}

$appExe = Join-Path $installDir "NotchGlass.exe"
$appIco = Join-Path $installDir "notchglass.ico"
$srcIco = Join-Path $RootDir "assets\notchglass.ico"
$srcHlsl = Join-Path $RootDir "shaders\LiquidGlassPS.hlsl"

Copy-Item -Force $builtExe $appExe
if (Test-Path $srcIco) { Copy-Item -Force $srcIco $appIco }
if (Test-Path $srcHlsl) { Copy-Item -Force $srcHlsl (Join-Path $installDir "LiquidGlassPS.hlsl") }

try {
    $wsh = New-Object -ComObject WScript.Shell
    $desktopPath = [Environment]::GetFolderPath("Desktop")
    if ($desktopPath -and (Test-Path $desktopPath)) {
        $desktopLnk = Join-Path $desktopPath "NotchGlass.lnk"
        $sc = $wsh.CreateShortcut($desktopLnk)
        $sc.TargetPath = $appExe
        $sc.WorkingDirectory = $installDir
        if (Test-Path $appIco) {
            $sc.IconLocation = "$appIco,0"
        } else {
            $sc.IconLocation = "$appExe,0"
        }
        $sc.Description = "NotchGlass - Liquid Glass Desktop Notch"
        $sc.Save()
        Write-Host ("  Desktop shortcut: " + $desktopLnk) -ForegroundColor Green

        $programsPath = [Environment]::GetFolderPath("Programs")
        if ($programsPath -and (Test-Path $programsPath)) {
            $startLnk = Join-Path $programsPath "NotchGlass.lnk"
            Copy-Item -Force $desktopLnk $startLnk -ErrorAction SilentlyContinue
        }

        $startupPath = [Environment]::GetFolderPath("Startup")
        if ($startupPath -and (Test-Path $startupPath)) {
            $startupLnk = Join-Path $startupPath "NotchGlass.lnk"
            Copy-Item -Force $desktopLnk $startupLnk -ErrorAction SilentlyContinue
        }
    }
} catch {
    Write-Host ("  Warning creating shortcut: " + $_.Exception.Message) -ForegroundColor Yellow
}

Write-Host "[5/5] Launching NotchGlass..." -ForegroundColor Cyan
try {
    & netsh advfirewall firewall delete rule name="NotchGlass Local Wi-Fi Transfer" 2>$null | Out-Null
    & netsh advfirewall firewall add rule name="NotchGlass Local Wi-Fi Transfer" dir=in action=allow program="$appExe" enable=yes profile=any 2>$null | Out-Null
} catch {}
Start-Process -FilePath $appExe -WorkingDirectory $installDir
Start-Sleep -Milliseconds 1200

$running = Get-Process -Name "NotchGlass" -ErrorAction SilentlyContinue
if (-not $running) {
    Write-Host "[ERROR] NotchGlass.exe exited immediately after launch." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "==================================================" -ForegroundColor Green
Write-Host "  NotchGlass is installed and running!" -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Green
Write-Host "  - Look at the top-center edge of your screen."
Write-Host "  - Press Ctrl + Alt + N anytime to toggle open/close."
Write-Host "  - Right-click the NotchGlass tray icon near the clock."
Write-Host "  - Installed folder: $installDir"
exit 0
