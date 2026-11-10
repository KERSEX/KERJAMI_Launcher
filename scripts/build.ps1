<#
.SYNOPSIS
    Configures and builds the launcher and optionally runs the tests.

.DESCRIPTION
    Tool locations are resolved in this order:
      1. script parameters
      2. environment variables LAUNCHER_QT_DIR, LAUNCHER_TOOLCHAIN_DIR,
         LAUNCHER_CMAKE, LAUNCHER_NINJA
      3. scripts/build.local.psd1 (machine-specific, not committed)
      4. PATH (cmake, ninja and the compiler)

.EXAMPLE
    .\scripts\build.ps1 -Test
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
    [string]$Config = 'Release',

    # Qt installation prefix, e.g. D:\Qt\6.11.2-static
    [string]$QtDir,

    # Root of an llvm-mingw toolchain. Leave empty to use the compiler from PATH
    # (for example inside a Visual Studio developer shell).
    [string]$ToolchainDir,

    [string]$CMakeExe,
    [string]$NinjaExe,

    # Run the test suite after a successful build.
    [switch]$Test,

    # Delete the build directory first.
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

$local = @{}
$localFile = Join-Path $PSScriptRoot 'build.local.psd1'
if (Test-Path -LiteralPath $localFile) {
    $local = Import-PowerShellDataFile -LiteralPath $localFile
}

function Resolve-Setting([string]$Value, [string]$EnvName, [string]$LocalKey) {
    if ($Value) { return $Value }
    $fromEnv = [Environment]::GetEnvironmentVariable($EnvName)
    if ($fromEnv) { return $fromEnv }
    if ($local.ContainsKey($LocalKey)) { return [string]$local[$LocalKey] }
    return ''
}

function Resolve-Tool([string]$Explicit, [string]$Name) {
    if ($Explicit) {
        if (-not (Test-Path -LiteralPath $Explicit)) { throw "$Name not found at '$Explicit'." }
        return $Explicit
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "$Name not found. Pass it as a parameter or create scripts\build.local.psd1 (see build.local.example.psd1)."
    }
    return $command.Source
}

function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "'$Exe' failed with exit code $LASTEXITCODE." }
}

$QtDir        = Resolve-Setting $QtDir        'LAUNCHER_QT_DIR'        'QtDir'
$ToolchainDir = Resolve-Setting $ToolchainDir 'LAUNCHER_TOOLCHAIN_DIR' 'ToolchainDir'
$cmake        = Resolve-Tool (Resolve-Setting $CMakeExe 'LAUNCHER_CMAKE' 'CMakeExe') 'cmake'
$ninja        = Resolve-Tool (Resolve-Setting $NinjaExe 'LAUNCHER_NINJA' 'NinjaExe') 'ninja'

if (-not $QtDir) { throw "Qt location unknown. Pass -QtDir or set it in scripts\build.local.psd1." }
if (-not (Test-Path -LiteralPath (Join-Path $QtDir 'lib\cmake\Qt6'))) {
    throw "'$QtDir' does not look like a Qt 6 installation prefix (lib\cmake\Qt6 is missing)."
}

$buildDir = Join-Path $repoRoot ("build\" + $Config.ToLowerInvariant())
if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    Remove-Item -LiteralPath $buildDir -Recurse -Force
}

$configureArgs = @(
    '-S', $repoRoot,
    '-B', $buildDir,
    '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$($ninja -replace '\\', '/')",
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_PREFIX_PATH=$($QtDir -replace '\\', '/')"
)

if ($ToolchainDir) {
    $toolchainBin = Join-Path $ToolchainDir 'bin'
    $compiler = Join-Path $toolchainBin 'clang++.exe'
    if (-not (Test-Path -LiteralPath $compiler)) { throw "No clang++.exe in '$toolchainBin'." }
    # The toolchain's own tools (linker, windres) must win over anything else in PATH.
    $env:PATH = "$toolchainBin;$env:PATH"
    $configureArgs += "-DCMAKE_CXX_COMPILER=$($compiler -replace '\\', '/')"
}

Write-Host "== Configure ($Config) ==" -ForegroundColor Cyan
Invoke-Checked $cmake $configureArgs

Write-Host "== Build ==" -ForegroundColor Cyan
Invoke-Checked $cmake @('--build', $buildDir)

if ($Test) {
    Write-Host "== Test ==" -ForegroundColor Cyan
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    Invoke-Checked $ctest @('--test-dir', $buildDir, '--output-on-failure')
}

Write-Host "Done: $(Join-Path $buildDir 'bin')" -ForegroundColor Green
