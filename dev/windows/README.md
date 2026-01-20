# Windows Development Setup

## Prerequisites

Install the following:

- **Visual Studio 2022** with "Desktop development with C++" workload
- **Python 3.12+**: `winget install Python.Python.3.12`
- **CMake**: `winget install Kitware.CMake`
- **Ninja**: `winget install Ninja-build.Ninja`
- **7-Zip**: `winget install 7zip.7zip`
- **Vulkan SDK**: `winget install KhronosGroup.VulkanSDK`
- **MSYS2** (optional, for mpv): https://www.msys2.org/

## Quick Start

```powershell
# Clone and setup
git clone https://github.com/jellyfin-labs/jellyfin-desktop-cef
cd jellyfin-desktop-cef
git submodule update --init --recursive

# Run setup (downloads CEF, mpv)
.\dev\windows\setup.ps1

# Build dependencies (from VS Developer Prompt)
# Open "x64 Native Tools Command Prompt for VS 2022" then:
.\dev\windows\build_cef.ps1
.\dev\windows\build_mpv.ps1

# Build the application
.\dev\windows\build.ps1
```

## Manual Setup

### 1. Git Submodules

```powershell
git submodule update --init --recursive
```

### 2. CEF (Chromium Embedded Framework)

```powershell
python dev\download_cef.py
```

### 3. libmpv

```powershell
.\dev\windows\download_mpv.ps1
```

Or manually download from [mpv-player-windows](https://sourceforge.net/projects/mpv-player-windows/files/libmpv/).

## Building

Must use Visual Studio Developer Command Prompt:

```powershell
# Configure
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build
cmake --build build
```

### CMake Options

| Option | Description |
|--------|-------------|
| `EXTERNAL_MPV_DIR` | Path to mpv installation (auto-detected from `third_party/mpv`) |
| `EXTERNAL_CEF_DIR` | Path to CEF installation (auto-detected from `third_party/cef`) |

## Scripts

| Script | Description |
|--------|-------------|
| `setup.ps1` | Full environment setup (CEF, mpv, checks prerequisites) |
| `build.ps1` | Configure and build with Ninja |
| `build_cef.ps1` | Build CEF wrapper library |
| `build_mpv.ps1` | Setup mpv import library (from MSYS2 or downloaded) |
| `download_mpv.ps1` | Download libmpv development files |

## Troubleshooting

### "MSVC environment not detected"

Run from "x64 Native Tools Command Prompt for VS 2022" or source vcvars:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
```

### CMake can't find Vulkan

Ensure `VULKAN_SDK` environment variable is set. May need to restart terminal after installing SDK.

### Import library for mpv

If using MSVC and `libmpv.dll.a` doesn't work, generate an import library:

```powershell
# From VS Developer Prompt
cd third_party\mpv
lib /def:libmpv-2.def /out:libmpv-2.lib /MACHINE:X64
```
