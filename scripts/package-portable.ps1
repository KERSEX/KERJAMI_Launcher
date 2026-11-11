<#
.SYNOPSIS
    Builds the portable release package of the launcher.

.DESCRIPTION
    Result (in -OutputDir, default dist\):
      <Product>-<version>-Windows-x64-Portable.zip   the release asset (D-39)
      <Product>-<version>-Windows-x64-Portable.zip.sha256   its checksum, one line "<hash>  <name>"

    The ZIP holds the launcher program at its root and update-manifest.json, which
    lists every file with size and SHA-256: the launcher's self-update installs exactly
    that list. With a static Qt the program is a single file. With a shared Qt (a Qt that
    has Qt6Core.dll) windeployqt copies the Qt libraries and plugins next to it.

    Steps: build (unless -SkipBuild) -> stage the files -> run the program's self-test on
    a copy -> write the manifest -> zip -> checksum -> verify-package.ps1.

    The version comes from the built program, so the package can never say anything else
    than the program does.

.EXAMPLE
    .\scripts\package-portable.ps1 -Test

.EXAMPLE
    .\scripts\package-portable.ps1 -SkipBuild -BinDir build\release\bin
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Config = 'Release',

    # Passed on to build.ps1.
    [string]$QtDir,
    [string]$ToolchainDir,
    [string]$CMakeExe,
    [string]$NinjaExe,

    # Folder with the built program. Default: build\<config>\bin.
    [string]$BinDir,

    # Where the package is written. Default: dist.
    [string]$OutputDir,

    # Do not build; package what is in -BinDir.
    [switch]$SkipBuild,

    # Run the test suite while building (ignored with -SkipBuild).
    [switch]$Test,

    # Do not run verify-package.ps1 at the end.
    [switch]$SkipVerify
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Write-Step([string]$Text) { Write-Host "== $Text ==" }

function Get-BrandValue([string]$Name) {
    $text = Get-Content -LiteralPath (Join-Path $repoRoot 'cmake\Branding.cmake') -Raw
    $match = [regex]::Match($text, 'set\(' + [regex]::Escape($Name) + '\s+"([^"]*)"')
    if (-not $match.Success) { throw "$Name not found in cmake\Branding.cmake." }
    return $match.Groups[1].Value
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function ConvertTo-JsonText([string]$Text) {
    # Only what appears in paths and names: backslash and quote. Everything else is plain ASCII.
    return '"' + $Text.Replace('\', '\\').Replace('"', '\"') + '"'
}

function Write-Utf8([string]$Path, [string]$Text) {
    # No byte order mark: the launcher's JSON reader and sha256sum-style readers do not want one.
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

# --- 1. build -----------------------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Step "Build ($Config)"
    $buildArguments = @{ Config = $Config }
    if ($QtDir) { $buildArguments.QtDir = $QtDir }
    if ($ToolchainDir) { $buildArguments.ToolchainDir = $ToolchainDir }
    if ($CMakeExe) { $buildArguments.CMakeExe = $CMakeExe }
    if ($NinjaExe) { $buildArguments.NinjaExe = $NinjaExe }
    if ($Test) { $buildArguments.Test = $true }
    & (Join-Path $PSScriptRoot 'build.ps1') @buildArguments
}

if (-not $BinDir) { $BinDir = Join-Path $repoRoot ('build\' + $Config.ToLowerInvariant() + '\bin') }
if (-not $OutputDir) { $OutputDir = Join-Path $repoRoot 'dist' }
$BinDir = [System.IO.Path]::GetFullPath($BinDir)
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)

$product = Get-BrandValue 'BRAND_PRODUCT_NAME'
$exeName = (Get-BrandValue 'BRAND_EXECUTABLE_NAME') + '.exe'
$program = Join-Path $BinDir $exeName
if (-not (Test-Path -LiteralPath $program)) { throw "$program not found. Build first, or pass -BinDir." }

# --- 2. stage -------------------------------------------------------------------------------
Write-Step 'Stage'
$stage = Join-Path $OutputDir 'stage'
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath $program -Destination $stage

# A shared Qt needs its libraries and plugins next to the program. A static Qt has none.
$qtBin = $null
if (-not $QtDir) { $QtDir = [Environment]::GetEnvironmentVariable('LAUNCHER_QT_DIR') }
if (-not $QtDir) {
    $localFile = Join-Path $PSScriptRoot 'build.local.psd1'
    if (Test-Path -LiteralPath $localFile) {
        $local = Import-PowerShellDataFile -LiteralPath $localFile
        if ($local.ContainsKey('QtDir')) { $QtDir = [string]$local['QtDir'] }
    }
}
if ($QtDir -and (Test-Path -LiteralPath (Join-Path $QtDir 'bin\Qt6Core.dll'))) { $qtBin = Join-Path $QtDir 'bin' }
if ($qtBin) {
    $deploy = Join-Path $qtBin 'windeployqt.exe'
    if (-not (Test-Path -LiteralPath $deploy)) { throw "Qt is shared but $deploy is missing." }
    Write-Host "Shared Qt: windeployqt copies the libraries and plugins."
    & $deploy --release --no-translations --no-opengl-sw --no-system-d3d-compiler --compiler-runtime (Join-Path $stage $exeName)
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE." }
} else {
    Write-Host 'Static Qt: the program is a single file.'
}

# --- 3. what is the program? (it says so itself) ------------------------------------------------
Write-Step 'Self-test of the staged program'
$probe = Join-Path $OutputDir 'probe'
if (Test-Path -LiteralPath $probe) { Remove-Item -LiteralPath $probe -Recurse -Force }
Copy-Item -LiteralPath $stage -Destination $probe -Recurse
$selfTestResult = Join-Path $OutputDir 'self-test.json'
if (Test-Path -LiteralPath $selfTestResult) { Remove-Item -LiteralPath $selfTestResult -Force }
$process = Start-Process -FilePath (Join-Path $probe $exeName) -ArgumentList @('--self-test', ('"' + $selfTestResult + '"')) `
    -WorkingDirectory $OutputDir -Wait -PassThru
if (-not (Test-Path -LiteralPath $selfTestResult)) { throw "The self-test wrote no result (exit code $($process.ExitCode))." }
$selfTest = Get-Content -LiteralPath $selfTestResult -Raw | ConvertFrom-Json
foreach ($check in $selfTest.checks) {
    $mark = if ($check.ok) { 'ok  ' } else { 'FAIL' }
    Write-Host ("  {0} {1,-16} {2}" -f $mark, $check.name, $check.detail)
}
Remove-Item -LiteralPath $probe -Recurse -Force
Remove-Item -LiteralPath $selfTestResult -Force
if ($process.ExitCode -ne 0 -or -not $selfTest.ok) { throw 'The self-test of the staged program failed. No package is made.' }
if ($selfTest.product -ne $product -or $selfTest.executable -ne $exeName) {
    throw "The program says it is $($selfTest.product)/$($selfTest.executable), the branding says $product/$exeName."
}
$version = [string]$selfTest.version
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "The program's version '$version' is not major.minor.patch." }

# --- 4. the manifest ------------------------------------------------------------------------------
Write-Step "Manifest for $product $version"
$stagePrefix = $stage.TrimEnd('\') + '\'
$entries = New-Object System.Collections.Generic.List[object]
foreach ($file in (Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($stagePrefix.Length).Replace('\', '/')
    $entries.Add([pscustomobject]@{ Path = $relative; Size = $file.Length; Sha256 = (Get-Sha256 $file.FullName) })
}
$lines = foreach ($entry in $entries) {
    '    {{"path": {0}, "size": {1}, "sha256": "{2}"}}' -f (ConvertTo-JsonText $entry.Path), $entry.Size, $entry.Sha256
}
$manifest = @(
    '{',
    '  "schema_version": 1,',
    ('  "product": {0},' -f (ConvertTo-JsonText $product)),
    ('  "version": {0},' -f (ConvertTo-JsonText $version)),
    ('  "launcher": {0},' -f (ConvertTo-JsonText $exeName)),
    '  "files": [',
    ($lines -join ",`n"),
    '  ]',
    '}'
) -join "`n"
Write-Utf8 (Join-Path $stage 'update-manifest.json') ($manifest + "`n")
foreach ($entry in $entries) { Write-Host ("  {0}  {1,12:N0} bytes  {2}" -f $entry.Sha256.Substring(0, 12), $entry.Size, $entry.Path) }

# --- 5. the ZIP -----------------------------------------------------------------------------------
Write-Step 'ZIP'
$zipName = '{0}-{1}-Windows-x64-Portable.zip' -f $product, $version
$zipPath = Join-Path $OutputDir $zipName
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
$stream = [System.IO.File]::Open($zipPath, [System.IO.FileMode]::CreateNew)
try {
    $archive = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        # Names with "/", the program first, the manifest last: the launcher's reader is strict about names.
        $names = @(Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object { $_.FullName.Substring($stagePrefix.Length).Replace('\', '/') })
        $ordered = @($names | Where-Object { $_ -ne 'update-manifest.json' } | Sort-Object) + @('update-manifest.json')
        foreach ($name in $ordered) {
            $source = Join-Path $stage ($name.Replace('/', '\'))
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $source, $name, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $archive.Dispose() }
} finally { $stream.Dispose() }

$zipHash = Get-Sha256 $zipPath
Write-Utf8 ($zipPath + '.sha256') ("{0}  {1}`n" -f $zipHash, $zipName)
Remove-Item -LiteralPath $stage -Recurse -Force

$zipSize = (Get-Item -LiteralPath $zipPath).Length
Write-Host ("  {0}  ({1:N1} MB)" -f $zipPath, ($zipSize / 1MB))
Write-Host "  SHA-256 $zipHash"

# --- 6. check it from the outside -------------------------------------------------------------------
if (-not $SkipVerify) {
    Write-Step 'Verify'
    & (Join-Path $PSScriptRoot 'verify-package.ps1') -Zip $zipPath
    if ($LASTEXITCODE -ne 0) { throw 'The package did not pass verify-package.ps1.' }
}

Write-Host ''
Write-Host "Done. Upload these two files as the assets of the release v$version of the launcher repository:"
Write-Host "  $zipPath"
Write-Host "  $zipPath.sha256"
