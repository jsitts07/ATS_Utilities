# ATS Utilities (Enhanced)

> [!NOTE]
> **Version 1.0.1** - Enhanced with robust pattern scanning and ATS SDK 1.14 compatibility
> This version includes comprehensive crash protection and debugging capabilities.

A plugin for ATS/ETS2 to add experimental trailer manipulation functionality with enhanced stability and compatibility.

## Recent Enhancements (v1.0.1)

### 🛡️ **Stability Improvements**
- **Robust Pattern Scanning**: Multiple fallback patterns for each hooked function
- **Crash Protection**: Automatic minidump generation and exception handling
- **Enhanced Logging**: Detailed debugging to `C:\Temp\ats_mod_log.txt`
- **Safe Memory Access**: Pointer validation and graceful error handling

### 🔧 **SDK 1.14 Compatibility** 
- Updated to use SCS SDK 1.14 for latest ATS versions
- Fixed telemetry initialization parameters
- Better error reporting for pattern scanning failures

### 🎯 **Better Debugging**
- Distinguishes between "no base controller", "no game actor", and "no trailers"
- Memory address logging for troubleshooting
- Pattern validation with detailed feedback

## Current features

 - Manually steerable trailer wheels
    - Abilty to take control of the steerable wheels on a trailer

   [Preview video](https://youtu.be/0kRavShaXy0)

 - Individually detachable trailers
    - Ability to disconnect and connect trailers individually and out of order
    - Still has multiple issues when trailers are disconnected, trailer cables, 3rd person camera, ...

   [Preview video](https://youtu.be/Nu6YvKKSL2g)

## Building

### Prerequisites
1. Install [Visual Studio Community 2022](https://visualstudio.microsoft.com/vs/community/) (free)
2. Select "Desktop development with C++" workload during installation
3. Ensure CMake tools are included

### Quick Setup
Run the setup script to check your build environment:
```powershell
PowerShell -ExecutionPolicy Bypass -File setup_build.ps1
```

### Build Commands
```powershell
# Configure CMake
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build Release version
cmake --build build --config Release
```

### VS Code Building
1. Open folder in VS Code (install C++ Extension Pack if prompted)
2. Press `Ctrl+Shift+P` → "CMake: Configure"  
3. Press `Ctrl+Shift+P` → "CMake: Build"

## Installation & Usage

1. Copy `ts-extra-utilities.dll` from `build/Release/` to ATS plugins folder
2. Launch ATS and attach trailers
3. Press `DELETE` key to open mod interface

## Troubleshooting

### "No trailers found" Error
- Check `C:\Temp\ats_mod_log.txt` for pattern scanning errors
- This usually means the mod needs updating for a new ATS version
- Look for lines like "Pattern X failed - no match found"

### Crashes
- Crash dumps are automatically saved to `C:\Temp\ats_mod_crash_TIMESTAMP.dmp`
- Include both the dump file and log file when reporting issues
- Use Debug build for more detailed crash information

### Build Issues
- Run `setup_build.ps1` to verify Visual Studio installation
- Ensure "Desktop development with C++" workload is installed
- Try cleaning: `cmake --build build --target clean`

 - Lockable trailer joints (PhysX only, `-physx` as a game launch parameter in Steam)
    - Ability to lock the joints between vehicle and trailers so you can stop them from pivoting

   [Preview video](https://youtu.be/zXtlzMVNEXM)

## How to use

Currently only works with DirectX11.

Made for singleplayer, **NOT** recommended in multiplayer.


Download the [latest release](https://github.com/dariowouters/ts-extra-utilities/releases/latest), copy the `ts-extra-utilities.dll` to `<game_install_location>/bin/win_x64/plugins`  
(if the plugins folder does not exists, you can create one)

Then in-game you can toggle the UI with `delete` and `insert` to toggle the cursor.  
(Keybinds are currently hardcoded, will make it changable at some point.)
