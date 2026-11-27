param(
    [string]$GameDir = "",
    [string]$ZigExe  = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$src      = Join-Path $repoRoot "src"
$build    = Join-Path $repoRoot "build"
$thirdPty = Join-Path $repoRoot "third_party"
$sdlVer   = "3.4.20"
$sdlDir   = Join-Path $thirdPty "SDL3-$sdlVer"
$sdlRoot  = Join-Path $sdlDir "i686-w64-mingw32"
$imguiVer = "1.91.8"
$imguiDir = Join-Path $thirdPty "imgui-$imguiVer"
$stbDir   = Join-Path $thirdPty "stb"

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
        throw "Zig compiler not found! Install Zig 0.14+ and add it to PATH, or pass -ZigExe <path>."
    }
}

New-Item -ItemType Directory -Force -Path $thirdPty, $build, $stbDir | Out-Null

# Auto-download official SDL3 MinGW dev package if not present
if (-not (Test-Path (Join-Path $sdlRoot "lib\libSDL3.dll.a"))) {
    $zipPath = Join-Path $thirdPty "SDL3-devel-$sdlVer-mingw.zip"
    if (-not (Test-Path $zipPath)) {
        $url = "https://github.com/libsdl-org/SDL/releases/download/release-$sdlVer/SDL3-devel-$sdlVer-mingw.zip"
        Write-Host "Downloading SDL3 $sdlVer MinGW dev package..."
        Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
    }
    Write-Host "Extracting SDL3 $sdlVer..."
    Expand-Archive -Path $zipPath -DestinationPath $thirdPty -Force
}

# Auto-download Dear ImGui if not present
if (-not (Test-Path (Join-Path $imguiDir "imgui.h"))) {
    $imguiZip = Join-Path $thirdPty "imgui-$imguiVer.zip"
    if (-not (Test-Path $imguiZip)) {
        $url = "https://github.com/ocornut/imgui/archive/refs/tags/v$imguiVer.zip"
        Write-Host "Downloading Dear ImGui v$imguiVer..."
        Invoke-WebRequest -Uri $url -OutFile $imguiZip -UseBasicParsing
    }
    Write-Host "Extracting Dear ImGui v$imguiVer..."
    Expand-Archive -Path $imguiZip -DestinationPath $thirdPty -Force
}

# Auto-download stb_image.h if not present
$stbH = Join-Path $stbDir "stb_image.h"
if (-not (Test-Path $stbH)) {
    Write-Host "Downloading stb_image.h..."
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" -OutFile $stbH -UseBasicParsing
}

@"
LIBRARY ddraw.dll
EXPORTS
    DirectDrawCreate=DirectDrawCreate@12
    DirectDrawEnumerateA=DirectDrawEnumerateA@8
"@ | Set-Content -Encoding ASCII (Join-Path $build "ddraw.def")

$imguiSources = @(
    "$imguiDir\imgui.cpp",
    "$imguiDir\imgui_draw.cpp",
    "$imguiDir\imgui_tables.cpp",
    "$imguiDir\imgui_widgets.cpp",
    "$imguiDir\backends\imgui_impl_sdl3.cpp",
    "$imguiDir\backends\imgui_impl_opengl3.cpp"
)

# Precompile Dear ImGui object files once (reused by both m1tp2_recomp.dll and m1tp2_native.exe)
$imguiObjs = @()
foreach ($s in $imguiSources) {
    $obj = Join-Path $build ([System.IO.Path]::GetFileNameWithoutExtension($s) + ".o")
    $imguiObjs += $obj
    if (-not (Test-Path $obj) -or (Get-Item $s).LastWriteTime -gt (Get-Item $obj).LastWriteTime) {
        Write-Host "Compiling $(Split-Path -Leaf $s)..."
        & $ZigExe c++ -target x86-windows-gnu -O2 -std=c++20 -c `
            -I"$sdlRoot\include" -I"$imguiDir" -I"$imguiDir\backends" `
            "$s" -o "$obj"
        if ($LASTEXITCODE -ne 0) { throw "Failed to compile $s" }
    }
}

Write-Host "Building m1tp2_recomp.dll (32-bit x86, SDL3 + OpenGL 3.3 + Dear ImGui)..."
& $ZigExe c++ -target x86-windows-gnu -O2 -std=c++20 -shared `
    -I"$sdlRoot\include" -I"$imguiDir" -I"$imguiDir\backends" -I"$stbDir" -I"$src" `
    "$src\core_main.cpp" `
    "$src\patch.cpp" `
    "$src\heap_guard.cpp" `
    "$src\vfs_mod.cpp" `
    "$src\campaign_hud.cpp" `
    "$src\gunnery.cpp" `
    "$src\audio_mixer.cpp" `
    "$src\gl_loader.cpp" `
    "$src\overlay_ui.cpp" `
    "$src\video_sdl.cpp" `
    "$src\glide_gl.cpp" `
    $imguiObjs `
    "$sdlRoot\lib\libSDL3.dll.a" -lopengl32 -luser32 -lkernel32 -lshell32 `
    -o "$build\m1tp2_recomp.dll"
if ($LASTEXITCODE -ne 0) { throw "Failed to build m1tp2_recomp.dll" }

Write-Host "Building zero-CRT native ddraw.dll bootstrap..."
& $ZigExe cc -target x86-windows-gnu -O2 -shared `
    "-Wl,-e,DllMain@12" `
    "$src\ddraw_shim.c" `
    "$build\ddraw.def" `
    -luser32 -lkernel32 `
    -o "$build\ddraw.dll"
if ($LASTEXITCODE -ne 0) { throw "Failed to build ddraw.dll" }

Write-Host "Building m1tp2_native.exe GUI Launcher (SDL3 + OpenGL 3.3 + Dear ImGui)..."
& $ZigExe c++ -target x86-windows-gnu -O2 -std=c++20 "-Wl,--subsystem,windows" `
    -I"$sdlRoot\include" -I"$imguiDir" -I"$imguiDir\backends" -I"$stbDir" `
    "$src\m1tp2_launcher.cpp" `
    $imguiObjs `
    "$sdlRoot\lib\libSDL3.dll.a" -lopengl32 -luser32 -lkernel32 -lshell32 `
    -o "$build\m1tp2_native.exe"
if ($LASTEXITCODE -ne 0) { throw "Failed to build m1tp2_native.exe" }

Copy-Item -Force "$sdlRoot\bin\SDL3.dll" "$build\SDL3.dll"

# Stage into game root if -GameDir passed OR parent folder contains M1TP2.exe
if ([string]::IsNullOrWhiteSpace($GameDir)) {
    $parentDir = Split-Path -Parent $repoRoot
    if (Test-Path (Join-Path $parentDir "M1TP2.exe")) {
        $GameDir = $parentDir
    }
}

if (-not [string]::IsNullOrWhiteSpace($GameDir) -and (Test-Path $GameDir)) {
    Copy-Item -Force "$build\m1tp2_recomp.dll" (Join-Path $GameDir "m1tp2_recomp.dll")
    Copy-Item -Force "$build\ddraw.dll"        (Join-Path $GameDir "ddraw.dll")
    Copy-Item -Force "$build\m1tp2_native.exe" (Join-Path $GameDir "m1tp2_native.exe")
    Copy-Item -Force "$build\SDL3.dll"         (Join-Path $GameDir "SDL3.dll")
    $keysSrc = Join-Path $repoRoot "docs\M1TP2_Quickstart_Keys.png"
    if (Test-Path $keysSrc) {
        $docsDst = Join-Path $GameDir "docs"
        New-Item -ItemType Directory -Force -Path $docsDst | Out-Null
        Copy-Item -Force $keysSrc (Join-Path $docsDst "M1TP2_Quickstart_Keys.png")
    }
    Write-Host "Staged binaries & quickstart docs to: $GameDir"
}

Write-Host "Build succeeded!"
Get-Item "$build\ddraw.dll", "$build\m1tp2_recomp.dll", "$build\m1tp2_native.exe", "$build\SDL3.dll" | Format-Table Name, Length, LastWriteTime -AutoSize
