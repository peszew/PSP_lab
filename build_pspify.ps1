<#
.SYNOPSIS
    Build script for PSPify (PSP Spotify Player) using Docker + pspdev toolchain.
    Run this script from the PSP_lab directory.

.DESCRIPTION
    1. Starts Docker Desktop if needed
    2. Pulls pspdev/pspdev image (cached after first run)
    3. Compiles PSPify inside the container
    4. Outputs EBOOT.PBP ready to deploy to your PSP memory stick

.DEPLOY
    Copy pspify\EBOOT.PBP  -->  ms0:\PSP\GAME\PSPify\EBOOT.PBP
    And add your MP3s and cover images (cover.jpg) to:
    ms0:\MUSIC\
#>

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PspifyDir = Join-Path $ScriptDir "pspify"

Write-Host ""
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host "  PSPify Music Player  -  Build script" -ForegroundColor Cyan
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host ""

# ---- Step 1: Check Docker ----------------------------------------------------
if (-not (Get-Command "docker" -ErrorAction SilentlyContinue)) {
    Write-Host "[ERROR] Docker is not installed." -ForegroundColor Red
    exit 1
}
Write-Host "[OK] Docker found." -ForegroundColor Green

# ---- Step 2: Start Daemon ----------------------------------------------------
$dockerOk = $false
try {
    docker info 2>&1 | Out-Null
    $dockerOk = $true
} catch { }

if (-not $dockerOk) {
    Write-Host "[INFO] Docker daemon not running. Starting Docker Desktop..." -ForegroundColor Yellow
    Start-Process "C:\Program Files\Docker\Docker\Docker Desktop.exe"
    Write-Host "[INFO] Waiting for Docker to start (up to 90 seconds)..." -ForegroundColor Yellow
    $waited = 0
    while ($waited -lt 90) {
        Start-Sleep -Seconds 5
        $waited += 5
        try {
            docker info 2>&1 | Out-Null
            $dockerOk = $true
            break
        } catch { }
        Write-Host "  Still waiting... ($waited s)" -ForegroundColor DarkGray
    }
    if (-not $dockerOk) {
        Write-Host "[ERROR] Docker did not start." -ForegroundColor Red
        exit 1
    }
}
Write-Host "[OK] Docker daemon is running." -ForegroundColor Green

# ---- Step 3: Compile inside container ----------------------------------------
Write-Host ""
Write-Host "[INFO] Building PSPify..." -ForegroundColor Yellow

docker run --rm `
    -v "${PspifyDir}:/build" `
    -w /build `
    pspdev/pspdev `
    sh -c "make clean && make"

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "[ERROR] Build failed." -ForegroundColor Red
    exit 1
}

# ---- Step 4: Confirm EBOOT.PBP exists -----------------------------------------
$eboot = Join-Path $PspifyDir "EBOOT.PBP"
if (-not (Test-Path $eboot)) {
    Write-Host "[ERROR] Build succeeded but EBOOT.PBP was not found." -ForegroundColor Red
    exit 1
}

$size = (Get-Item $eboot).Length
Write-Host ""
Write-Host "======================================================" -ForegroundColor Green
Write-Host "  BUILD SUCCESSFUL!" -ForegroundColor Green
Write-Host "  EBOOT.PBP  ($([math]::Round($size/1KB, 1)) KB)" -ForegroundColor Green
Write-Host "======================================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Deployment instructions:" -ForegroundColor Cyan
Write-Host "  1. Connect your PSP via USB or insert memory stick"
Write-Host "  2. Create folder:  ms0:\PSP\GAME\PSPify\"
Write-Host "  3. Copy:  pspify\EBOOT.PBP  -->  ms0:\PSP\GAME\PSPify\EBOOT.PBP"
Write-Host "  4. Copy your music to:  ms0:\MUSIC\"
Write-Host "  5. Put album art in same folder as the songs, named:"
Write-Host "     'cover.jpg', 'cover.png', 'folder.jpg', or 'folder.png'"
Write-Host ""

# ---- Optional: copy to the PSP_lab deploy path -------------------------------
$deployDest = Join-Path $ScriptDir "PSP\GAME\PSPify"
if (-not (Test-Path $deployDest)) {
    New-Item -ItemType Directory -Path $deployDest | Out-Null
}
Copy-Item -Path $eboot -Destination (Join-Path $deployDest "EBOOT.PBP") -Force
Write-Host "[INFO] Also copied EBOOT.PBP to: $deployDest" -ForegroundColor DarkGray
Write-Host ""
