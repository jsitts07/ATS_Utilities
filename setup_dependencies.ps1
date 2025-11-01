# Setup Dependencies Script
# This script downloads and sets up the required dependencies for ATS Utilities

Write-Host "=== Setting up ATS Utilities Dependencies ===" -ForegroundColor Green
Write-Host ""

# Create temporary directory for downloads
$tempDir = Join-Path $env:TEMP "ats_dependencies"
if (Test-Path $tempDir) { Remove-Item $tempDir -Recurse -Force }
New-Item -ItemType Directory -Path $tempDir | Out-Null

# Current directory should be the project root
$projectRoot = Get-Location
$vendorDir = Join-Path $projectRoot "vendor"

Write-Host "Project root: $projectRoot" -ForegroundColor Cyan
Write-Host "Vendor directory: $vendorDir" -ForegroundColor Cyan
Write-Host ""

# Function to download and extract
function Download-And-Extract {
    param($Url, $DestinationFolder, $Name)
    
    Write-Host "Downloading $Name..." -ForegroundColor Yellow
    $zipFile = Join-Path $tempDir "$Name.zip"
    
    try {
        Invoke-WebRequest -Uri $Url -OutFile $zipFile -UseBasicParsing
        Write-Host "Extracting $Name..." -ForegroundColor Yellow
        Expand-Archive -Path $zipFile -DestinationPath $tempDir -Force
        
        # Find the extracted folder (usually has version number)
        $extractedFolders = Get-ChildItem $tempDir -Directory | Where-Object { $_.Name -like "*$Name*" -or $_.Name -like "*$($Name.ToLower())*" }
        if ($extractedFolders.Count -gt 0) {
            $sourceFolder = $extractedFolders[0].FullName
            Copy-Item -Path "$sourceFolder\*" -Destination $DestinationFolder -Recurse -Force
            Write-Host "✓ $Name installed successfully" -ForegroundColor Green
        } else {
            Write-Host "✗ Could not find extracted folder for $Name" -ForegroundColor Red
        }
    } catch {
        Write-Host "✗ Failed to download/extract $Name`: $_" -ForegroundColor Red
    }
}

# Download and setup fmt library
Write-Host "1. Setting up fmt library..." -ForegroundColor Magenta
$fmtDir = Join-Path $vendorDir "fmt"
New-Item -ItemType Directory -Path $fmtDir -Force | Out-Null
Download-And-Extract "https://github.com/fmtlib/fmt/archive/refs/tags/10.1.1.zip" $fmtDir "fmt"

# Download and setup MinHook
Write-Host "`n2. Setting up MinHook library..." -ForegroundColor Magenta  
$minhookDir = Join-Path $vendorDir "minhook"
New-Item -ItemType Directory -Path $minhookDir -Force | Out-Null
Download-And-Extract "https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.3.zip" $minhookDir "minhook"

# Download and setup Dear ImGui
Write-Host "`n3. Setting up Dear ImGui library..." -ForegroundColor Magenta
$imguiDir = Join-Path $vendorDir "imgui"  
New-Item -ItemType Directory -Path $imguiDir -Force | Out-Null
Download-And-Extract "https://github.com/ocornut/imgui/archive/refs/tags/v1.89.9.zip" $imguiDir "imgui"

# Cleanup
Write-Host "`nCleaning up temporary files..." -ForegroundColor Yellow
Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "=== Dependencies Setup Complete ===" -ForegroundColor Green
Write-Host ""
Write-Host "You can now build the project:" -ForegroundColor Yellow
Write-Host "1. Configure: cmake -S . -B build -G 'Visual Studio 17 2022' -A x64" -ForegroundColor Cyan
Write-Host "2. Build: cmake --build build --config Release" -ForegroundColor Cyan
Write-Host ""