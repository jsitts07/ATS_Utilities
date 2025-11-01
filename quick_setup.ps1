# Quick dependency setup - downloading individual files
Write-Host "Setting up dependencies..." -ForegroundColor Green

# Create fmt minimal setup
$fmtDir = "vendor\fmt"
New-Item -ItemType Directory -Path $fmtDir -Force
New-Item -ItemType Directory -Path "$fmtDir\include\fmt" -Force

# Download fmt header (header-only version)
Write-Host "Downloading fmt..." -ForegroundColor Yellow
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/fmtlib/fmt/10.1.1/include/fmt/core.h" -OutFile "$fmtDir\include\fmt\core.h"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/fmtlib/fmt/10.1.1/include/fmt/format.h" -OutFile "$fmtDir\include\fmt\format.h"

# Create basic CMakeLists for fmt
@"
add_library(fmt-header-only INTERFACE)
target_include_directories(fmt-header-only INTERFACE include)
"@ | Out-File -FilePath "$fmtDir\CMakeLists.txt" -Encoding utf8

Write-Host "fmt setup complete" -ForegroundColor Green