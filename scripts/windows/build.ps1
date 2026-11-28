<#
.SYNOPSIS
Build PeakEmi on Windows and start it.

.DESCRIPTION
The whole loop in one command: find Visual Studio's C++ compiler, Qt, CMake
and Ninja; configure the preset the first time; build; put the Qt libraries
next to the executable; and start PeakEmi.

    scripts\windows\build.ps1                  # build the release preset, start it
    scripts\windows\build.ps1 -NoRun           # build only
    scripts\windows\build.ps1 -Test            # build, run the tests, start it
    scripts\windows\build.ps1 -Preset debug    # another configure preset
    scripts\windows\build.ps1 -Restart         # close a running PeakEmi first

A PeakEmi started from the build folder locks peakemi.exe, so the build stops
when one is running. -Restart asks it to close first, as clicking its close
button would: with unsaved changes it asks whether to save them.

Only what this script finds missing is looked for: run it from a Visual Studio
developer prompt, or with Qt, CMake and Ninja on PATH, and those are used.

.PARAMETER Preset
The configure and build preset from CMakePresets.json. Default: release.

.PARAMETER QtDir
The Qt kit to build against, e.g. C:\Qt\6.8.1\msvc2022_64. Default: the
first entry of CMAKE_PREFIX_PATH, else the newest C:\Qt\6.x.y\msvc2022_64.

.PARAMETER NoRun
Build, but do not start PeakEmi.

.PARAMETER Test
Run the test suite after building; stop if a test fails.

.PARAMETER Restart
Close a PeakEmi running from this build before building.
#>
[CmdletBinding()]
param(
    [ValidateSet('release', 'relwithdebinfo', 'debug', 'dev', 'ci')]
    [string] $Preset = 'release',
    [string] $QtDir = '',
    [switch] $NoRun,
    [switch] $Test,
    [switch] $Restart
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildDir = Join-Path $root "build\$Preset"
$binDir = Join-Path $buildDir 'bin'
$exe = Join-Path $binDir 'peakemi.exe'
$debugBuild = $Preset -in @('debug', 'dev')

function Write-Step([string] $Text) {
    Write-Host "==> $Text" -ForegroundColor Cyan
}

function Stop-Build([string] $Text) {
    Write-Host "error: $Text" -ForegroundColor Red
    exit 1
}

function Add-ToPath([string] $Directory) {
    $env:PATH = "$Directory;$env:PATH"
}

# --- A PeakEmi running from this build locks its executable -----------------
$running = @(Get-Process -Name peakemi -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and ($_.Path -ieq $exe) })
if ($running.Count -gt 0) {
    if (-not $Restart) {
        Stop-Build "PeakEmi is running from $binDir. Close it, or run again with -Restart."
    }
    Write-Step 'Asking the running PeakEmi to close'
    foreach ($process in $running) {
        [void] $process.CloseMainWindow()
        if (-not $process.WaitForExit(120000)) {
            Stop-Build 'PeakEmi did not close. It may be asking whether to save the session.'
        }
    }
}

# --- The C++ compiler ---------------------------------------------------------
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        Stop-Build 'Visual Studio 2022 or its Build Tools were not found (no vswhere.exe).'
    }
    $vs = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1
    if (-not $vs) {
        Stop-Build 'No Visual Studio installation has the C++ x64 build tools.'
    }
    Write-Step "Using the C++ compiler of $vs"
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

# --- Qt -------------------------------------------------------------------------
if (-not $QtDir) {
    if ($env:CMAKE_PREFIX_PATH) {
        $QtDir = ($env:CMAKE_PREFIX_PATH -split ';')[0]
    } else {
        $QtDir = Get-ChildItem -Path 'C:\Qt' -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^6\.\d+\.\d+$' } |
            Sort-Object { [version] $_.Name } -Descending |
            ForEach-Object { Join-Path $_.FullName 'msvc2022_64' } |
            Where-Object { Test-Path (Join-Path $_ 'bin\windeployqt.exe') } |
            Select-Object -First 1
    }
}
if (-not $QtDir -or -not (Test-Path (Join-Path $QtDir 'bin\windeployqt.exe'))) {
    Stop-Build ('Qt 6 for MSVC 2022 was not found. Pass -QtDir C:\Qt\6.8.1\msvc2022_64, ' +
        'or set CMAKE_PREFIX_PATH to the kit.')
}
$QtDir = (Resolve-Path $QtDir).Path
$env:CMAKE_PREFIX_PATH = $QtDir
Add-ToPath (Join-Path $QtDir 'bin')
Write-Step "Using Qt from $QtDir"

# --- CMake and Ninja ------------------------------------------------------------
# A build folder keeps using the CMake and Ninja that configured it.
$cache = Join-Path $buildDir 'CMakeCache.txt'
if (Test-Path $cache) {
    foreach ($entry in 'CMAKE_COMMAND', 'CMAKE_MAKE_PROGRAM') {
        $line = Select-String -Path $cache -Pattern "^${entry}:[A-Z]+=(.+)$" | Select-Object -First 1
        if ($line) {
            $tool = $line.Matches[0].Groups[1].Value
            if (Test-Path $tool) {
                Add-ToPath (Split-Path -Parent $tool)
            }
        }
    }
}
foreach ($tool in 'cmake', 'ninja') {
    if (-not (Get-Command "$tool.exe" -ErrorAction SilentlyContinue)) {
        # Where "py -m pip install --user cmake ninja" puts them.
        $found = Get-ChildItem -Path (Join-Path $env:APPDATA 'Python') -Recurse `
            -Filter "$tool.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $found) {
            Stop-Build "$tool was not found. Install it, e.g. with: py -m pip install --user cmake ninja"
        }
        Add-ToPath $found.DirectoryName
    }
}

Push-Location $root
try {
    # --- Configure the first time, build every time ---------------------------
    if (-not (Test-Path $cache)) {
        Write-Step "Configuring the $Preset preset"
        & cmake --preset $Preset "-DCMAKE_PREFIX_PATH=$QtDir"
        if ($LASTEXITCODE -ne 0) {
            Stop-Build 'CMake could not configure the build; its output above says why.'
        }
    }
    Write-Step "Building the $Preset preset"
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        Stop-Build 'The build failed; the compiler output above says why.'
    }

    # --- The Qt libraries next to the executables -----------------------------
    # Cheap when nothing changed: windeployqt only copies what is newer.
    Write-Step 'Putting the Qt libraries next to PeakEmi'
    $mode = if ($debugBuild) { '--debug' } else { '--release' }
    foreach ($program in 'peakemi.exe', 'peakemi-cli.exe') {
        $path = Join-Path $binDir $program
        if (Test-Path $path) {
            & windeployqt.exe $mode --no-translations --compiler-runtime $path | Out-Null
            if ($LASTEXITCODE -ne 0) {
                Stop-Build "windeployqt could not deploy $program."
            }
        }
    }
    # The tests and the headless runner use the offscreen platform, which
    # windeployqt leaves out.
    $offscreen = if ($debugBuild) { 'qoffscreend.dll' } else { 'qoffscreen.dll' }
    $platforms = Join-Path $binDir 'platforms'
    New-Item -ItemType Directory -Force -Path $platforms | Out-Null
    Copy-Item (Join-Path $QtDir "plugins\platforms\$offscreen") $platforms -Force

    # --- Tests --------------------------------------------------------------------
    if ($Test) {
        Write-Step 'Running the tests'
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
        try {
            & ctest --test-dir $buildDir --output-on-failure
            $failed = $LASTEXITCODE -ne 0
        } finally {
            Remove-Item Env:QT_QPA_PLATFORM, Env:QT_QPA_FONTDIR -ErrorAction SilentlyContinue
        }
        if ($failed) {
            Stop-Build 'Some tests failed; see above.'
        }
    }
} finally {
    Pop-Location
}

# --- Start it ---------------------------------------------------------------------
if (-not $NoRun) {
    Write-Step "Starting $exe"
    Start-Process -FilePath $exe -WorkingDirectory $binDir
}
