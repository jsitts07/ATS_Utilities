# ATS Mod Build Setup Script
# This script helps set up the build environment for the ts-extra-utilities mod

Write-Host "=== ATS Mod Build Setup ===" -ForegroundColor Green
Write-Host ""

# Check for existing Visual Studio installations
Write-Host "Checking for Visual Studio installations..." -ForegroundColor Yellow

$vsInstallations = @()

# Check for VS 2022
$vs2022Paths = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional", 
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
)

foreach ($path in $vs2022Paths) {
    if (Test-Path $path) {
        $vsInstallations += $path
        Write-Host "Found: $path" -ForegroundColor Green
    }
}

# Check for VS 2019
$vs2019Paths = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools"
)

foreach ($path in $vs2019Paths) {
    if (Test-Path $path) {
        $vsInstallations += $path
        Write-Host "Found: $path" -ForegroundColor Green
    }
}

if ($vsInstallations.Count -eq 0) {
    Write-Host "No Visual Studio installations found!" -ForegroundColor Red
    Write-Host ""
    Write-Host "BUILD OPTIONS:" -ForegroundColor Yellow
    Write-Host "1. Install Visual Studio Community 2022 (free, full IDE)"
    Write-Host "   Download from: https://visualstudio.microsoft.com/vs/community/"
    Write-Host "   - Select 'Desktop development with C++' workload"
    Write-Host "   - This includes CMake, MSBuild, and IntelliSense"
    Write-Host ""
    Write-Host "2. Install Build Tools for Visual Studio 2022 (lighter)"
    Write-Host "   Download from: https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022"
    Write-Host "   - Select 'C++ build tools' workload"
    Write-Host ""
    Write-Host "3. Use an alternative compiler (advanced users)"
    Write-Host "   - MinGW-w64 with CMake"
    Write-Host "   - Clang with CMake"
    Write-Host ""
    
    $choice = Read-Host "Which option would you like to pursue? (1/2/3)"
    
    switch ($choice) {
        "1" { 
            Write-Host "Opening Visual Studio Community download page..." -ForegroundColor Green
            Start-Process "https://visualstudio.microsoft.com/vs/community/"
        }
        "2" { 
            Write-Host "Opening Build Tools download page..." -ForegroundColor Green
            Start-Process "https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022"
        }
        "3" {
            Write-Host "For MinGW-w64 setup:" -ForegroundColor Green
            Write-Host "1. Install MSYS2 from https://www.msys2.org/"
            Write-Host "2. Run: pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake"
            Write-Host "3. Add to PATH: C:\msys64\mingw64\bin"
        }
        default {
            Write-Host "Invalid choice. Please run the script again." -ForegroundColor Red
        }
    }
    
    Write-Host ""
    Write-Host "After installing build tools, run this script again to continue setup." -ForegroundColor Yellow
    
} else {
    Write-Host "Found $($vsInstallations.Count) Visual Studio installation(s)" -ForegroundColor Green
    
    # Try to find MSBuild
    $msbuildPaths = @()
    foreach ($vsPath in $vsInstallations) {
        $msbuildPath = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
        if (Test-Path $msbuildPath) {
            $msbuildPaths += $msbuildPath
            Write-Host "Found MSBuild: $msbuildPath" -ForegroundColor Green
        }
    }
    
    if ($msbuildPaths.Count -gt 0) {
        Write-Host ""
        Write-Host "BUILD READY!" -ForegroundColor Green
        Write-Host "You can now build the project using one of these methods:" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "METHOD 1 - CMake (Recommended):"
        Write-Host "1. Install CMake from https://cmake.org/download/ (if not already installed)"
        Write-Host "2. Run: cmake -S . -B build -G `"Visual Studio 17 2022`" -A x64"
        Write-Host "3. Run: cmake --build build --config Release"
        Write-Host ""
        Write-Host "METHOD 2 - Visual Studio IDE:"
        Write-Host "1. Open Visual Studio"
        Write-Host "2. File -> Open -> Folder... and select this directory"
        Write-Host "3. Use the CMake targets to build"
        Write-Host ""
        Write-Host "METHOD 3 - Developer Command Prompt:"
        Write-Host "1. Open 'Developer Command Prompt for VS'"
        Write-Host "2. Navigate to this directory"
        Write-Host "3. Run the CMake commands above"
    }
}

Write-Host ""
Write-Host "Current directory: $PWD" -ForegroundColor Cyan
Write-Host "Project files: CMakeLists.txt found: $(Test-Path 'CMakeLists.txt')" -ForegroundColor Cyan