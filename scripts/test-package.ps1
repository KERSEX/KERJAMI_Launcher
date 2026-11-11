<#
.SYNOPSIS
    Test of the packaging scripts: a good package passes, broken ones are refused.

.DESCRIPTION
    Run by CTest (PackageTests). Builds the package from the program in -BinDir with
    package-portable.ps1 (which verifies it), then makes broken packages out of it and
    checks that verify-package.ps1 refuses every one of them. A verification that cannot
    say "no" is worth nothing.

    Exit code 0: everything behaved. 1: something did not.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BinDir,

    [string]$OutputDir
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

if (-not $OutputDir) { $OutputDir = Join-Path ([System.IO.Path]::GetTempPath()) ('package-test-' + [guid]::NewGuid().ToString('N').Substring(0, 8)) }
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
if (Test-Path -LiteralPath $OutputDir) { Remove-Item -LiteralPath $OutputDir -Recurse -Force }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$failures = New-Object System.Collections.Generic.List[string]
function Expect([string]$Name, [bool]$Condition) {
    if ($Condition) { Write-Host "  ok   $Name" } else { Write-Host "  FAIL $Name"; $failures.Add($Name) }
}

# --- a good package ------------------------------------------------------------------------------
Write-Host '== The package of the built program =='
& (Join-Path $PSScriptRoot 'package-portable.ps1') -SkipBuild -BinDir $BinDir -OutputDir $OutputDir
Expect 'package-portable.ps1 builds a package that verify-package.ps1 accepts' ($LASTEXITCODE -eq 0)
$good = @(Get-ChildItem -LiteralPath $OutputDir -Filter '*-Windows-x64-Portable.zip')
Expect 'exactly one ZIP and a checksum file' (($good.Count -eq 1) -and (Test-Path -LiteralPath ($good[0].FullName + '.sha256')))
if ($good.Count -ne 1) { exit 1 }
$goodZip = $good[0].FullName

# --- broken ones ----------------------------------------------------------------------------------
$tamper = Join-Path $OutputDir 'tamper'
New-Item -ItemType Directory -Path $tamper | Out-Null

function New-Variant([string]$Name, [scriptblock]$Change) {
    # Unpacks the good package, lets $Change break it, and packs it again under the same name.
    $folder = Join-Path $tamper $Name
    $files = Join-Path $folder 'files'
    New-Item -ItemType Directory -Path $files -Force | Out-Null
    [System.IO.Compression.ZipFile]::ExtractToDirectory($goodZip, $files)
    & $Change $files
    $zip = Join-Path $folder (Split-Path -Leaf $goodZip)
    $stream = [System.IO.File]::Open($zip, [System.IO.FileMode]::CreateNew)
    $archive = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create)
    $prefix = $files.TrimEnd('\') + '\'
    foreach ($file in (Get-ChildItem -LiteralPath $files -Recurse -File)) {
        $entry = $file.FullName.Substring($prefix.Length).Replace('\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry) | Out-Null
    }
    $archive.Dispose(); $stream.Dispose()
    # With a right checksum file: the only thing wrong with a variant is what $Change did to it.
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    [System.IO.File]::WriteAllText($zip + '.sha256', ("{0}  {1}`n" -f $hash, (Split-Path -Leaf $zip)), (New-Object System.Text.UTF8Encoding($false)))
    return $zip
}

# The verification must refuse the package, and for the reason that was built in:
# every check named in $Expected has to fail, and the checksum of the ZIP must not be one of them.
function Test-Refused([string]$Name, [string]$Zip, [string[]]$Expected) {
    $resultFile = $Zip + '.result.json'
    & (Join-Path $PSScriptRoot 'verify-package.ps1') -Zip $Zip -ResultFile $resultFile | Out-Null
    $code = $LASTEXITCODE
    $failed = @((Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json) | Where-Object { -not $_.ok } | ForEach-Object { $_.name })
    Expect "refused: $Name" ($code -ne 0)
    foreach ($check in $Expected) { Expect "      because of: $check" ($failed -contains $check) }
    Expect '      not for a missing or wrong checksum file' ($failed -notcontains 'checksum file')
}

Write-Host '== Broken packages must be refused =='
Test-Refused 'a debug file in the package' (New-Variant 'pdb' { param($f) Set-Content -LiteralPath (Join-Path $f 'KERJAMI_Launcher.pdb') -Value 'debug' }) @('manifest lists the files', 'no development leftovers')
Test-Refused 'a file that the manifest does not list' (New-Variant 'unlisted' { param($f) Set-Content -LiteralPath (Join-Path $f 'extra.dll') -Value 'x' }) @('manifest lists the files')
Test-Refused 'a program that is not the one the manifest names' (New-Variant 'altered' {
    param($f)
    $path = Join-Path $f 'KERJAMI_Launcher.exe'
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 0xFF
    [System.IO.File]::WriteAllBytes($path, $bytes)
}) @('manifest lists the files', 'accepted as a self-update package')
Test-Refused 'a manifest for another version' (New-Variant 'version' {
    param($f)
    $path = Join-Path $f 'update-manifest.json'
    $text = (Get-Content -LiteralPath $path -Raw) -replace '"version": "[^"]*"', '"version": "9.9.9"'
    [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($false)))
}) @('manifest header', 'accepted as a self-update package')
Test-Refused 'a package without the manifest' (New-Variant 'nomanifest' { param($f) Remove-Item -LiteralPath (Join-Path $f 'update-manifest.json') }) @('required files')
Test-Refused 'a program that holds a user name' (New-Variant 'username' {
    param($f)
    # Appends the name of the user running this test to the program; the manifest is renewed so that only the privacy check can object.
    $path = Join-Path $f 'KERJAMI_Launcher.exe'
    $stream = [System.IO.File]::Open($path, [System.IO.FileMode]::Append)
    $bytes = [System.Text.Encoding]::ASCII.GetBytes("C:\Users\" + $env:USERNAME + "\build")
    $stream.Write($bytes, 0, $bytes.Length); $stream.Dispose()
    $manifest = Join-Path $f 'update-manifest.json'
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $size = (Get-Item -LiteralPath $path).Length
    $text = Get-Content -LiteralPath $manifest -Raw
    $text = [regex]::Replace($text, '"size": \d+, "sha256": "[0-9a-f]{64}"', ('"size": ' + $size + ', "sha256": "' + $hash + '"'))
    [System.IO.File]::WriteAllText($manifest, $text, (New-Object System.Text.UTF8Encoding($false)))
}) @('no personal paths or names')

Remove-Item -LiteralPath $tamper -Recurse -Force
Write-Host ''
if ($failures.Count -eq 0) { Write-Host 'The packaging scripts do what they say.'; exit 0 }
Write-Host ("{0} expectation(s) failed: {1}" -f $failures.Count, ($failures -join '; '))
exit 1
