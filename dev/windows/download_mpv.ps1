# Download and setup libmpv for Windows development
# Based on jellyfin-desktop's Windows build workflow

param(
    [string]$Version = "20251214-git-f7be2ee",
    [string]$Arch = "x86_64-v3",
    [string]$OutputDir = "third_party\mpv"
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Get-Item $PSScriptRoot).Parent.Parent.FullName
$OutputPath = Join-Path $RepoRoot $OutputDir

Write-Host "Downloading libmpv $Version for $Arch..."

# Create temp directory
$TempDir = Join-Path $env:TEMP "mpv-download-$(Get-Random)"
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

try {
    # Download mpv dev package
    $Url = "https://sourceforge.net/projects/mpv-player-windows/files/libmpv/mpv-dev-$Arch-$Version.7z/download"
    $ArchivePath = Join-Path $TempDir "mpv.7z"

    Write-Host "Downloading from $Url"
    Invoke-WebRequest -Uri $Url -OutFile $ArchivePath -UseBasicParsing

    # Extract
    Write-Host "Extracting..."
    $ExtractDir = Join-Path $TempDir "extracted"
    & 7z x $ArchivePath -o"$ExtractDir" -y
    if ($LASTEXITCODE -ne 0) { throw "7z extraction failed" }

    # Setup output directory structure (matches EXTERNAL_MPV_DIR expectations)
    if (Test-Path $OutputPath) {
        Remove-Item -Recurse -Force $OutputPath
    }
    New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null
    $LibDir = Join-Path $OutputPath "lib"
    New-Item -ItemType Directory -Path $LibDir -Force | Out-Null

    # Move include to output root, libs to lib/
    Move-Item (Join-Path $ExtractDir "include") $OutputPath
    Move-Item (Join-Path $ExtractDir "libmpv-2.dll") $LibDir

    # Generate .def file if gendef is available, otherwise use existing
    $DefFile = Join-Path $ExtractDir "libmpv-2.def"
    if (-not (Test-Path $DefFile)) {
        $DllPath = Join-Path $LibDir "libmpv-2.dll"
        if (Get-Command gendef -ErrorAction SilentlyContinue) {
            Push-Location $LibDir
            & gendef libmpv-2.dll
            Pop-Location
            $DefFile = Join-Path $LibDir "libmpv-2.def"
        }
    }

    if (Test-Path $DefFile) {
        Move-Item $DefFile (Join-Path $LibDir "libmpv-2.def") -ErrorAction SilentlyContinue
    }

    # Copy import library if present
    $ImportLib = Join-Path $ExtractDir "libmpv.dll.a"
    if (Test-Path $ImportLib) {
        Move-Item $ImportLib (Join-Path $LibDir "libmpv.dll.a")
    }

    # Generate MSVC import library if in VS environment and def file exists
    $MsvcLib = Join-Path $LibDir "libmpv-2.lib"
    $DefPath = Join-Path $LibDir "libmpv-2.def"
    if ((Test-Path $DefPath) -and $env:VSINSTALLDIR -and (Get-Command lib.exe -ErrorAction SilentlyContinue)) {
        Write-Host "Generating MSVC import library..."
        Push-Location $LibDir
        & lib.exe /def:libmpv-2.def /out:libmpv-2.lib /MACHINE:X64
        Pop-Location
        if (Test-Path $MsvcLib) {
            Write-Host "Generated $MsvcLib"
        }
    }

    Write-Host "libmpv installed to $OutputPath"
    Write-Host ""
    Write-Host "Contents:"
    Get-ChildItem $OutputPath -Recurse | ForEach-Object {
        Write-Host "  $($_.FullName.Substring($OutputPath.Length + 1))"
    }
}
finally {
    # Cleanup
    if (Test-Path $TempDir) {
        Remove-Item -Recurse -Force $TempDir
    }
}
