param(
    [string]$Version = "0.3.2",
    [string]$ZigExe  = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$src      = Join-Path $repoRoot "src"
$build    = Join-Path $repoRoot "build"
$dist     = Join-Path $repoRoot "dist"
$tools    = Join-Path $repoRoot "tools"
$thirdPty = Join-Path $repoRoot "third_party"

# Locate Zig compiler
if ([string]::IsNullOrWhiteSpace($ZigExe)) {
    if ($env:ZIG_EXE -and (Test-Path $env:ZIG_EXE)) {
        $ZigExe = $env:ZIG_EXE
    } elseif (Get-Command zig -ErrorAction SilentlyContinue) {
        $ZigExe = (Get-Command zig).Source
    } elseif (Test-Path (Join-Path $thirdPty "zig\zig-x86_64-windows-0.14.1\zig.exe")) {
        $ZigExe = Join-Path $thirdPty "zig\zig-x86_64-windows-0.14.1\zig.exe"
    } elseif (Test-Path "C:\tools\zig\zig-x86_64-windows-0.14.1\zig.exe") {
        $ZigExe = "C:\tools\zig\zig-x86_64-windows-0.14.1\zig.exe"
    } else {
        throw "Zig compiler not found!"
    }
}

# 1. Build core binaries
& (Join-Path $repoRoot "build.ps1") -ZigExe $ZigExe

# 2. Build Win32 GUI installer stub (with Common-Controls v6 + asInvoker manifest)
Write-Host "Building native Win32 GUI installer stub..."
$resFile = Join-Path $build "installer_res.res"
& $ZigExe rc /fo "$resFile" "$src\installer.rc"
if ($LASTEXITCODE -ne 0) { throw "Failed to compile installer.rc" }

$stubExe = Join-Path $build "installer_stub.exe"
& $ZigExe cc -target x86-windows-gnu -O2 "-Wl,--subsystem,windows" `
    "$src\installer_gui.c" "$resFile" `
    -luser32 -lkernel32 -lgdi32 -ladvapi32 -lshell32 -lole32 -luuid -lcomctl32 `
    -o $stubExe
if ($LASTEXITCODE -ne 0) { throw "Failed to build installer_stub.exe" }

# 3. Generate mods\README_MODS.txt for bundling
$modsReadme = Join-Path $build "README_MODS.txt"
@"
====================================================================
  M1 Tank Platoon II - Native Modding VFS (mods/ directory)
====================================================================

HOW TO INSTALL MODS:
  1. Create a folder for each mod inside this mods/ directory,
     for example:  mods\hd_textures\
                   mods\custom_battles\
  2. Place modified files inside your mod folder matching either:
     - Catalog paths (overrides m1.cat / 3dz.cat with NO limit):
         mods\my_mod\3dz\m1a2_50.3dz
         mods\my_mod\pics\m1a2_h.pcx
         mods\my_mod\vpics\m1a2_h.pcx
         mods\my_mod\cockpits\commander\c-cpit_4.pcx
     - Or root-mirrored disk paths:
         mods\my_mod\Game\battle.txt
         mods\my_mod\Cockpits\cockpit.dat
         mods\my_mod\BATTLES\ARMOR\ARMOR_1.TXT
         mods\my_mod\World\NTC\Tiles.txt

LOAD ORDER:
  - Optional: create mods\loadorder.txt listing one mod folder name
    per line (lines starting with # are comments). Later entries
    override earlier entries.
  - If loadorder.txt is absent, all folders in mods\ load in
    alphabetical order, with mods\override\ loaded last (highest).

IN-GAME:
  - Press F12 to toggle Mods VFS ON/OFF and view live override stats.
  - Check m1tp2_recomp.log to see every overridden asset loaded.
"@ | Set-Content -Encoding ASCII $modsReadme

# 4. Pack 1-shot single-file GUI installer
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$setupExe = Join-Path $dist "M1TP2-Native-Setup-v$Version.exe"
python (Join-Path $tools "pack_installer.py") $stubExe $setupExe `
    "ddraw.dll=$build\ddraw.dll" `
    "m1tp2_recomp.dll=$build\m1tp2_recomp.dll" `
    "m1tp2_native.exe=$build\m1tp2_native.exe" `
    "SDL3.dll=$build\SDL3.dll" `
    "THIRD_PARTY_LICENSES.txt=$repoRoot\THIRD_PARTY_LICENSES.txt" `
    "docs\M1TP2_Quickstart_Keys.png=$repoRoot\docs\M1TP2_Quickstart_Keys.png" `
    "mods\README_MODS.txt=$modsReadme" `
    "tools\m1tp2_modtool.py=$tools\m1tp2_modtool.py"
if ($LASTEXITCODE -ne 0) { throw "Failed to pack installer!" }

# 5. Build Portable ZIP
$portDir = Join-Path $build "portable_stage"
if (Test-Path $portDir) { Remove-Item -Recurse -Force $portDir }
New-Item -ItemType Directory -Force -Path "$portDir\mods", "$portDir\tools", "$portDir\docs" | Out-Null
Copy-Item "$build\ddraw.dll"                "$portDir\ddraw.dll"
Copy-Item "$build\m1tp2_recomp.dll"         "$portDir\m1tp2_recomp.dll"
Copy-Item "$build\m1tp2_native.exe"         "$portDir\m1tp2_native.exe"
Copy-Item "$build\SDL3.dll"                 "$portDir\SDL3.dll"
Copy-Item "$repoRoot\THIRD_PARTY_LICENSES.txt" "$portDir\THIRD_PARTY_LICENSES.txt"
Copy-Item "$repoRoot\README.md"             "$portDir\README_NATIVE.md"
Copy-Item "$repoRoot\docs\M1TP2_Quickstart_Keys.png" "$portDir\docs\M1TP2_Quickstart_Keys.png"
Copy-Item "$modsReadme"                     "$portDir\mods\README_MODS.txt"
Copy-Item "$tools\m1tp2_modtool.py"         "$portDir\tools\m1tp2_modtool.py"

$portZip = Join-Path $dist "M1TP2-Native-v$Version-portable.zip"
if (Test-Path $portZip) { Remove-Item -Force $portZip }
Compress-Archive -Path "$portDir\*" -DestinationPath $portZip -CompressionLevel Optimal

# 6. Write SHA256SUMS.txt
$sumsPath = Join-Path $dist "SHA256SUMS.txt"
$h1 = (Get-FileHash $setupExe -Algorithm SHA256).Hash.ToLower()
$h2 = (Get-FileHash $portZip  -Algorithm SHA256).Hash.ToLower()
@(
    "$h1  M1TP2-Native-Setup-v$Version.exe"
    "$h2  M1TP2-Native-v$Version-portable.zip"
) | Set-Content -Encoding ASCII $sumsPath

Write-Host "`nRelease artifacts ready in $dist :"
Get-Item $setupExe, $portZip, $sumsPath | Format-Table Name, Length, LastWriteTime -AutoSize
Get-Content $sumsPath
