<#
.SYNOPSIS
    Build script for PSP Snake game using Docker + pspdev toolchain.
    Run this script from the PSP_lab directory.

.DESCRIPTION
    1. Starts Docker Desktop if needed
    2. Pulls pspdev/pspdev image (first run only)
    3. Compiles the game inside the container
    4. Outputs EBOOT.PBP ready to deploy to your PSP memory stick

.DEPLOY
    Copy snake\EBOOT.PBP  -->  ms0:\PSP\GAME\Snake\EBOOT.PBP
    Then launch from XMB: Game > Memory Stick > Snake
#>

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SnakeDir  = Join-Path $ScriptDir "snake"

Write-Host ""
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host "  PSP Snake  -  Build script" -ForegroundColor Cyan
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host ""

# ---- Step 1: Check Docker is installed ----------------------------------------
if (-not (Get-Command "docker" -ErrorAction SilentlyContinue)) {
    Write-Host "[ERROR] Docker is not installed." -ForegroundColor Red
    Write-Host "  Install Docker Desktop from: https://www.docker.com/products/docker-desktop"
    exit 1
}
Write-Host "[OK] Docker found." -ForegroundColor Green

# ---- Step 2: Start Docker Desktop if the daemon is not running ----------------
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
        Write-Host "[ERROR] Docker did not start in time. Please start Docker Desktop manually and re-run." -ForegroundColor Red
        exit 1
    }
}
Write-Host "[OK] Docker daemon is running." -ForegroundColor Green

# ---- Step 3: Pull pspdev image (cached after first pull) ----------------------
Write-Host ""
Write-Host "[INFO] Pulling pspdev/pspdev Docker image (first run may take a few minutes)..." -ForegroundColor Yellow
docker pull pspdev/pspdev
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Failed to pull pspdev/pspdev image." -ForegroundColor Red
    exit 1
}
Write-Host "[OK] pspdev image ready." -ForegroundColor Green

# ---- Step 4: Build inside container -------------------------------------------
Write-Host ""
Write-Host "[INFO] Building Snake..." -ForegroundColor Yellow

# Convert Windows path to Docker-compatible path
$DockerPath = $SnakeDir -replace '\\', '/' -replace '^([A-Za-z]):', '/$1'
$DockerPath = $DockerPath.ToLower() -replace '^/([a-z])/', '/$1/'

docker run --rm `
    -v "${SnakeDir}:/build" `
    -w /build `
    pspdev/pspdev `
    sh -c "make clean && make"

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "[ERROR] Build failed. Check the output above for errors." -ForegroundColor Red
    exit 1
}

# ---- Step 5: Confirm EBOOT.PBP exists -----------------------------------------
$eboot = Join-Path $SnakeDir "EBOOT.PBP"
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
Write-Host "  Next steps:" -ForegroundColor Cyan
Write-Host "  1. Connect your PSP via USB or insert memory stick"
Write-Host "  2. Create folder:  ms0:\PSP\GAME\Snake\"
Write-Host "  3. Copy:  snake\EBOOT.PBP  -->  ms0:\PSP\GAME\Snake\EBOOT.PBP"
Write-Host "  4. Launch from XMB: Game > Memory Stick > Snake"
Write-Host ""

# ---- Optional: auto-copy to the PSP_lab deploy path --------------------------
$deployDest = Join-Path $ScriptDir "PSP\GAME\Snake"
if (-not (Test-Path $deployDest)) {
    New-Item -ItemType Directory -Path $deployDest | Out-Null
}
Copy-Item -Path $eboot -Destination (Join-Path $deployDest "EBOOT.PBP") -Force
Write-Host "[INFO] Also copied EBOOT.PBP to: $deployDest" -ForegroundColor DarkGray
Write-Host "       (mirrors the PSP memory stick folder structure in this workspace)" -ForegroundColor DarkGray
Write-Host ""
